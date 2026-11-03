/**
 * Essentia's WebAssembly glue, made loadable under this App's Content Security Policy.
 *
 * The pinned `essentia-wasm` build is Emscripten output from before `-sDYNAMIC_EXECUTION=0`
 * existed, and its embind glue builds three invokers out of strings with `new Function(...)`:
 *
 * 1. `createNamedFunction` — one per registered class and error constructor;
 * 2. `embind__requireFunction`'s `makeDynCaller` — one per registered method, and this is the
 *    one that fires during `new Essentia(...)`, before the first Track is measured;
 * 3. `craftEmvalAllocator` — a JavaScript constructor invoked from wasm.
 *
 * The App's policy is `script-src 'self' 'wasm-unsafe-eval' blob:` (`vercel.json`): WebAssembly
 * may compile, JavaScript may not be made out of strings. The blob URL is therefore loaded and
 * the first `new Function` inside it is refused — the analyser never initialises, and every
 * Track fails with "Evaluating a string as JavaScript violates the following Content Security
 * Policy directive".
 *
 * So the text is rewritten, before it becomes a blob, into the closures those string bodies
 * would have produced. Each site must be found exactly once: a site this build does not know
 * is an error naming it, not a shrug. A new Essentia version has to be re-audited rather than
 * silently loaded and failing at the first Track.
 */

interface Site {
  /** What the site is, for the refusal when it is not found. */
  what: string;
  /** The exact text in the pinned build (`essentia.js@0.1.3`, Emscripten 2.x embind). */
  find: string;
  /** The closure the string body would have produced, written out. */
  replace: string;
}

/**
 * The `new Function` token the sites below are found by, assembled rather than written out:
 * `grep` cannot tell a search string from a call, and a bundle that matches both makes the
 * "does this app build code from strings?" audit answer "maybe". `join` keeps the compiler
 * from folding the halves back together.
 */
const DYNAMIC = ['new', 'Function'].join(' ');

const SITES: readonly Site[] = [
  {
    what: 'createNamedFunction',
    find: String.raw`function createNamedFunction(name,body){name=makeLegalFunctionName(name);return ${DYNAMIC}("body","return function "+name+"() {\n"+'    "use strict";'+"    return body.apply(this, arguments);\n"+"};\n")(body)}`,
    // The name is cosmetic (embind uses it for stack traces and never for identity), so it is
    // set where the engine allows it and dropped where it does not.
    replace:
      'function createNamedFunction(name,body){name=makeLegalFunctionName(name);' +
      'var fn=function(){"use strict";return body.apply(this,arguments)};' +
      'try{Object.defineProperty(fn,"name",{value:name,configurable:true})}catch(e){}return fn}',
  },
  {
    what: 'embind__requireFunction',
    find: String.raw`function makeDynCaller(dynCall){var args=[];for(var i=1;i<signature.length;++i){args.push("a"+i)}var name="dynCall_"+signature+"_"+rawFunction;var body="return function "+name+"("+args.join(", ")+") {\n";body+="    return dynCall(rawFunction"+(args.length?", ":"")+args.join(", ")+");\n";body+="};\n";return ${DYNAMIC}("dynCall","rawFunction",body)(dynCall,rawFunction)}`,
    // What the generated body did: `dynCall(rawFunction, a1, ... aN)`.
    replace:
      'function makeDynCaller(dynCall){return function(){var a=[rawFunction];' +
      'for(var i=0;i<arguments.length;++i){a.push(arguments[i])}return dynCall.apply(null,a)}}',
  },
  {
    what: 'craftEmvalAllocator',
    find: `return ${DYNAMIC}("requireRegisteredType","Module","__emval_register",functionBody)(requireRegisteredType,Module,__emval_register)`,
    // What the generated body did: read each argument through its registered type, construct,
    // and register the result. `Reflect.construct` is the spread-free way to say `new C(...)`.
    replace:
      'return function(constructor,argTypes,args){var a=[];' +
      'for(var i=0;i<argCount;++i){var t=requireRegisteredType(Module["HEAP32"][(argTypes>>>2)+i],"parameter "+i);' +
      'a.push(t.readValueFromPointer(args));args+=t["argPackAdvance"]}' +
      'return __emval_register(Reflect.construct(constructor,a))}',
  },
];

/** Raised when the pinned glue is not the shape this build knows how to rewrite. */
export class EssentiaGlueError extends Error {
  constructor(readonly site: string) {
    super(
      `Essentia's WebAssembly glue has a "${site}" this build does not recognise, so it cannot ` +
        'be loaded without eval, which this origin forbids. Re-audit the pinned version.',
    );
    this.name = 'EssentiaGlueError';
  }
}

/**
 * The glue text with its dynamic code written out. Total: either every site is replaced or
 * this throws naming the one that is missing. (Not idempotent, on purpose — a second pass has
 * nothing to find, and silently accepting the text would hide that it was already patched.)
 */
export function patchEssentiaGlue(text: string): string {
  let out = text;
  for (const site of SITES) {
    const at = out.indexOf(site.find);
    if (at === -1) throw new EssentiaGlueError(site.what);
    out = out.slice(0, at) + site.replace + out.slice(at + site.find.length);
  }
  return out;
}
