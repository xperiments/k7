/** The slice of ONNX Runtime the analyzer needs, so browser and Node can share it. */
export interface TensorData {
  data: Float32Array;
  dims: readonly number[];
}

export interface Session {
  inputNames: readonly string[];
  outputNames: readonly string[];
  run(feeds: Record<string, TensorData>): Promise<Record<string, TensorData>>;
}

export interface Runtime {
  /** The caller has already fetched and verified the model bytes. */
  createSession(model: Uint8Array): Promise<Session>;
}

export function embeddingOutput(session: Session): string {
  return (
    session.outputNames.find((name) => /embedding/i.test(name)) ??
    session.outputNames[session.outputNames.length - 1]
  );
}
