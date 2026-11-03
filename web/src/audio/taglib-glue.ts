/**
 * TagLib's WebAssembly glue, made loadable under this App's Content Security Policy.
 *
 * This is a **build-time** transform, applied by `vite.config.ts` to
 * `taglib-wasm/dist/taglib-wrapper.js` before it is bundled. It has to be build-time: the
 * glue is imported as part of the shell, not fetched as a weight, so there is no text to
 * rewrite at run time the way `weights/essentia-glue.ts` does.
 *
 * The pinned build (`taglib-wasm@2.4.0`, Emscripten 4.x) builds two invokers with
 * `new Function(...)`, and the first runs during `TagLib.initialize()` — measured: 74
 * calls before the first tag is written, and 4 more while `edit()` runs a callback into
 * wasm. Under `script-src 'self' 'wasm-unsafe-eval' blob:` (vercel.json) every one of them
 * is refused, so **Save tags** throws a Content Security Policy violation instead of
 * writing a file.
 *
 * Each site is replaced by the closure its generated body produced, using the variables
 * already in scope at that point. Every site must be found exactly once, so a new TagLib
 * version fails the build here rather than at the button.
 */

interface Site {
  /** What the site is, for the refusal when it is not found. */
  what: string;
  find: string;
  replace: string;
}

/**
 * `createJsInvoker`'s factory. The generated body wires each argument through its
 * registered type, calls the C++ invoker, runs destructors and converts the result; the
 * closure below does that over `argTypes`, which `createJsInvoker` already holds.
 *
 * The caller passes the same positional captures the generated function declared
 * (`humanName, throwBindingError, invoker, fn, runDestructors, fromRetWire,
 * toClassParamWire`); the destructor functions are read from `argTypes` instead of being
 * passed as extra arguments, which is why the trailing captures are ignored.
 */
const JS_INVOKER: Site = {
  what: 'createJsInvoker',
  find: 'return new Function(args1,invokerFnBody)',
  replace:
    'return function(humanName,throwBindingError,invoker,fn,runDestructors,fromRetWire,toClassParamWire){' +
    'var wires=[],dtors=[];' +
    'for(var i=0;i<argCount;++i){var t=argTypes[i+2];wires.push(t.toWireType.bind(t));dtors.push(t.destructorFunction||null)}' +
    'return function(){' +
    'var destructors=needsDestructorStack?[]:null;var wired=[];' +
    'if(isClassMethodFunc){wired.push(toClassParamWire(destructors,this))}' +
    'for(var i=0;i<argCount;++i){wired.push(wires[i](destructors,arguments[i]))}' +
    'var rv=invoker.apply(null,[fn].concat(wired));' +
    'if(needsDestructorStack){runDestructors(destructors)}' +
    'else{for(var i=0;i<dtors.length;++i){if(dtors[i]){dtors[i](wired[(isClassMethodFunc?1:0)+i])}}}' +
    'return returns?fromRetWire(rv):undefined' +
    '}}',
};

/**
 * `__emval_create_invoker`'s method caller: the function wasm calls when it needs to run a
 * JavaScript method — our `edit` callback is one. `kind` selects call, method call,
 * construct, or evaluate-the-arguments.
 */
const EMVAL_INVOKER: Site = {
  what: '__emval_create_invoker',
  find: 'var invokerFunction=new Function(Object.keys(captures),functionBody)(...Object.values(captures));',
  replace:
    'var invokerFunction=(function(){' +
    'return function(handle,methodName,destructorsRef,args){' +
    'var argv=[];' +
    'for(var i=0;i<argFromPtr.length;++i){argv.push(argFromPtr[i](args+i*GenericWireTypeSize))}' +
    // kind 3 evaluates the arguments and uses no handle at all, which is why the lookup
    // is inside the branch: `Emval.toValue(0)` would throw.
    'var result;' +
    'if(kind===3){result=argv.length?argv[argv.length-1]:undefined}' +
    'else{var holder=Emval.toValue(handle);' +
    'if(kind===2){result=Reflect.construct(holder,argv)}' +
    'else if(kind===1){result=holder[getStringOrSymbol(methodName)].apply(holder,argv)}' +
    'else{result=holder.apply(null,argv)}}' +
    'return retType.isVoid?undefined:emval_returnValue(toReturnWire,destructorsRef,result)' +
    '}})();',
};

const SITES: readonly Site[] = [JS_INVOKER, EMVAL_INVOKER];

/** Raised when the pinned glue is not the shape this build knows how to rewrite. */
export class TaglibGlueError extends Error {
  constructor(readonly site: string) {
    super(
      `TagLib's WebAssembly glue has no "${site}" site this build recognises, so it cannot be ` +
        'loaded without eval, which this origin forbids. Re-audit the pinned version.',
    );
    this.name = 'TaglibGlueError';
  }
}

/** The glue text with its dynamic code written out. Total: every site or an error. */
export function patchTaglibGlue(text: string): string {
  let out = text;
  for (const site of SITES) {
    const at = out.indexOf(site.find);
    if (at === -1) throw new TaglibGlueError(site.what);
    out = out.slice(0, at) + site.replace + out.slice(at + site.find.length);
  }
  return out;
}
