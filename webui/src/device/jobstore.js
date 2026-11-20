// Where job records (device/jobrecord.js) are kept: IndexedDB in this browser, database "maslow", object store
// "jobs", keyed by the record's id. Every call is wrapped so the page works without storage (a private window,
// blocked site data): then the records are kept in memory until the page closes, and `persistent` is false.

const DB = "maslow", STORE = "jobs";

export function memoryStore() {
  const recs = new Map();
  return {
    persistent: false,
    load: async () => [...recs.values()],
    save: async (r) => { recs.set(r.id, r); },
    remove: async (id) => { recs.delete(id); }
  };
}

const done = (req) => new Promise((resolve, reject) => { req.onsuccess = () => resolve(req.result); req.onerror = () => reject(req.error); });

export function openJobStore() {
  let idb;
  try { idb = globalThis.indexedDB; } catch { idb = null; }
  if (!idb) return memoryStore();
  const fallback = memoryStore();
  let open;
  try {
    const req = idb.open(DB, 1);
    req.onupgradeneeded = () => { if (!req.result.objectStoreNames.contains(STORE)) req.result.createObjectStore(STORE, { keyPath: "id" }); };
    open = done(req);
  } catch (e) {
    open = Promise.reject(e);
  }
  const store = {
    persistent: true,
    // Each call falls back to memory if the database can't be used
    async load() {
      try { const db = await open; return await done(db.transaction(STORE).objectStore(STORE).getAll()); }
      catch { store.persistent = false; return fallback.load(); }
    },
    async save(rec) {
      try { const db = await open; await done(db.transaction(STORE, "readwrite").objectStore(STORE).put(rec)); }
      catch { store.persistent = false; await fallback.save(rec); }
    },
    async remove(id) {
      try { const db = await open; await done(db.transaction(STORE, "readwrite").objectStore(STORE).delete(id)); }
      catch { await fallback.remove(id); }
    }
  };
  return store;
}
