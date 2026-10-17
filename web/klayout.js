// A minimal client for the klayout-web protocol (crates/klayout-web/src/protocol.rs).
// `call` sends one request, with an optional binary payload, and returns
// { reply, events, payload }. A reply of type "error" throws.
//
// The payload of a "frame" reply is a view into the module's memory, valid
// until the next call; everything else is copied.

export async function connect(createKlayout, { editable = true } = {}) {
  const k = await createKlayout();
  const encoder = new TextEncoder();
  const decoder = new TextDecoder();

  function copyIn(bytes) {
    if (!bytes || bytes.length === 0) return 0;
    const p = k._malloc(bytes.length);
    k.HEAPU8.set(bytes, p);
    return p;
  }

  function call(request, payload) {
    const json = encoder.encode(JSON.stringify(request));
    const jp = copyIn(json);
    const pp = copyIn(payload);
    let response;
    try {
      const rp = k._klw_call(jp, json.length, pp, payload?.length ?? 0);
      response = JSON.parse(decoder.decode(k.HEAPU8.subarray(rp, rp + k._klw_response_len())));
    } finally {
      if (jp) k._free(jp);
      if (pp) k._free(pp);
    }
    const p = k._klw_payload(), n = k._klw_payload_len();
    const bytes = n ? k.HEAPU8.subarray(p, p + n) : null;
    response.payload = bytes && response.reply.type !== "frame" ? bytes.slice() : bytes;
    if (response.reply.type === "error") {
      const error = new Error(`${request.type}: ${response.reply.message}`);
      error.events = response.events;
      throw error;
    }
    return response;
  }

  const hello = call({ type: "hello", protocol: 1, editable }).reply;
  return { call, hello };
}
