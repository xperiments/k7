import 'fake-indexeddb/auto';
import { describe, expect, it, vi } from 'vitest';

import {
  cardHandleFor,
  ensureAccess,
  forgetCardHandle,
  queryAccess,
  rememberCardHandle,
} from '../src/card/handles';

/**
 * The card's handle, kept for the next visit.
 *
 * What survives a reload is the handle; what never does is the grant, so the module asks for
 * it on the gesture that needs it. The fake here is a plain object — IndexedDB can clone it,
 * and the real thing is cloned the same way.
 */

const markA = 'mark:00000000-0000-4000-8000-00000000b001';
const markB = 'mark:00000000-0000-4000-8000-00000000b002';

const directory = (name: string) =>
  ({ kind: 'directory', name }) as unknown as FileSystemDirectoryHandle;

describe('the remembered card handle', () => {
  it('is one per card, so another card’s handle cannot read its paths', async () => {
    await rememberCardHandle(markA, directory('SDCARD-A'));
    await rememberCardHandle(markB, directory('SDCARD-B'));

    expect(((await cardHandleFor(markA)) as unknown as { name: string }).name).toBe('SDCARD-A');
    expect(((await cardHandleFor(markB)) as unknown as { name: string }).name).toBe('SDCARD-B');
    expect(await cardHandleFor('mark:unknown')).toBeNull();

    await forgetCardHandle(markA);
    expect(await cardHandleFor(markA)).toBeNull();
  });
});

describe('asking for the grant', () => {
  const permissioned = (query: PermissionState, request: PermissionState) =>
    ({
      kind: 'directory',
      name: 'SDCARD',
      queryPermission: vi.fn(async () => query),
      requestPermission: vi.fn(async () => request),
    }) as unknown as FileSystemDirectoryHandle;

  it('does not ask when the grant is already there', async () => {
    const handle = permissioned('granted', 'granted');
    expect(await ensureAccess(handle, 'read')).toBe(true);
    expect((handle as unknown as { requestPermission: unknown }).requestPermission).not.toHaveBeenCalled;
  });

  it('asks when the grant has to be renewed, and says what it got', async () => {
    expect(await ensureAccess(permissioned('prompt', 'granted'), 'readwrite')).toBe(true);
    expect(await ensureAccess(permissioned('prompt', 'denied'), 'read')).toBe(false);
  });

  it('proceeds in a browser that grants on use, or in a test stand-in', async () => {
    expect(await ensureAccess(directory('SDCARD'), 'read')).toBe(true);
  });
});

describe('asking whether the grant is already there', () => {
  it('answers without prompting, which is what a page load may do', async () => {
    const granted = {
      kind: 'directory',
      name: 'SDCARD',
      queryPermission: async () => 'granted',
      requestPermission: vi.fn(async () => 'denied'),
    } as unknown as FileSystemDirectoryHandle;

    expect(await queryAccess(granted, 'read')).toBe(true);
    expect(
      (granted as unknown as { requestPermission: unknown }).requestPermission,
    ).not.toHaveBeenCalled();
  });

  it('is false while the grant still needs a gesture, and false when asked of a stub', async () => {
    const prompt = {
      kind: 'directory',
      name: 'SDCARD',
      queryPermission: async () => 'prompt',
    } as unknown as FileSystemDirectoryHandle;

    expect(await queryAccess(prompt, 'read')).toBe(false);
    expect(await queryAccess(directory('SDCARD'), 'read')).toBe(true);
  });
});
