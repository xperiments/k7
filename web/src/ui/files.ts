export interface FoundFile {
  file: File;
  /** Location inside the dropped folder, including the file name. */
  path: string;
}

export interface ScanResult {
  files: FoundFile[];
  /** Entries the browser refused to read (permissions, broken handles, …). */
  skipped: number;
}

const ignored = (name: string) => name.startsWith('.') || name === '__MACOSX';

const ignoredPath = (path: string) =>
  path.split('/').some((segment) => segment.startsWith('.') || segment === '__MACOSX');

function readAll(reader: FileSystemDirectoryReader): Promise<FileSystemEntry[]> {
  return new Promise((resolve, reject) => {
    const entries: FileSystemEntry[] = [];
    const read = () =>
      reader.readEntries((batch) => {
        if (batch.length === 0) {
          resolve(entries);
          return;
        }
        entries.push(...batch);
        read();
      }, reject);
    read();
  });
}

function fileOf(entry: FileSystemFileEntry): Promise<File> {
  return new Promise((resolve, reject) => entry.file(resolve, reject));
}

async function walk(
  directory: FileSystemDirectoryEntry,
  base: string,
  files: FoundFile[],
  skipped: { count: number },
): Promise<void> {
  let children: FileSystemEntry[];
  try {
    children = await readAll(directory.createReader());
  } catch {
    skipped.count++;
    return;
  }

  children.sort((a, b) => a.name.localeCompare(b.name));

  for (const child of children) {
    if (ignored(child.name)) continue;
    const path = `${base}${child.name}`;

    if (child.isFile) {
      try {
        files.push({ file: await fileOf(child as FileSystemFileEntry), path });
      } catch {
        skipped.count++;
      }
    } else if (child.isDirectory) {
      await walk(child as FileSystemDirectoryEntry, `${path}/`, files, skipped);
    }
  }
}

/**
 * Walks a drop's directory entries. `readEntries` returns batches (Chrome caps
 * them at 100), so it must be called until empty. Files are sorted by name to
 * make the resulting queue deterministic.
 */
export async function scanDataTransfer(data: DataTransfer): Promise<ScanResult> {
  const entries: FileSystemEntry[] = [];
  for (const item of Array.from(data.items)) {
    if (item.kind !== 'file') continue;
    const entry = item.webkitGetAsEntry?.();
    if (entry) entries.push(entry);
  }

  if (entries.length === 0) {
    return {
      files: Array.from(data.files)
        .filter((file) => !ignoredPath(file.name))
        .map((file) => ({ file, path: file.name })),
      skipped: 0,
    };
  }

  const files: FoundFile[] = [];
  const skipped = { count: 0 };

  for (const entry of entries) {
    if (entry.isDirectory) {
      await walk(entry as FileSystemDirectoryEntry, `${entry.name}/`, files, skipped);
    } else if (entry.isFile) {
      try {
        files.push({
          file: await fileOf(entry as FileSystemFileEntry),
          path: entry.name,
        });
      } catch {
        skipped.count++;
      }
    }
  }

  return { files, skipped: skipped.count };
}

/** The folder picker input supplies `webkitRelativePath` on every file. */
export function filesFromInput(files: FileList): FoundFile[] {
  return Array.from(files)
    .map((file) => ({ file, path: file.webkitRelativePath || file.name }))
    .filter((found) => !ignoredPath(found.path));
}
