(() => {
    const request = item => new Promise((resolve, reject) => {
        item.onsuccess = () => resolve(item.result);
        item.onerror = () => reject(item.error);
    });
    const complete = transaction => new Promise((resolve, reject) => {
        transaction.oncomplete = resolve;
        transaction.onabort = transaction.onerror = () => reject(transaction.error || new Error('game-data storage failed.'));
    });
    async function open() {
        const item = indexedDB.open(Nectar.game === 'pikmin2' ? 'nectar-pikmin2-own-game' : 'nectar-own-game', 1);
        item.onupgradeneeded = () => {
            item.result.createObjectStore('installs');
            item.result.createObjectStore('files');
        };
        return request(item);
    }
    async function write(db, store, key, value) {
        const transaction = db.transaction(store, 'readwrite');
        const done = complete(transaction);
        transaction.objectStore(store).put(value, key);
        await done;
    }
    async function read(db, store, key) {
        return request(db.transaction(store).objectStore(store).get(key));
    }
    async function remove(db, id) {
        const transaction = db.transaction(['installs', 'files'], 'readwrite');
        const done = complete(transaction);
        transaction.objectStore('installs').delete(id);
        const cursor = transaction.objectStore('files').openKeyCursor(IDBKeyRange.bound([id, ''], [id, '\uffff']));
        cursor.onsuccess = () => {
            if (!cursor.result) return;
            transaction.objectStore('files').delete(cursor.result.primaryKey);
            cursor.result.continue();
        };
        await done;
    }
    async function active(db) {
        const id = await read(db, 'installs', 'active');
        if (!id) return null;
        const install = await read(db, 'installs', id);
        if (!install || install.state !== 'ready') return null;
        return install;
    }
    async function commit(db, install) {
        const transaction = db.transaction('installs', 'readwrite');
        const done = complete(transaction);
        const store = transaction.objectStore('installs');
        store.put({...install, state: 'ready'}, install.id);
        store.put(install.id, 'active');
        await done;
    }
    async function clean(db, keepId) {
        const keys = await request(db.transaction('installs').objectStore('installs').getAllKeys());
        for (const id of keys) if (id !== 'active' && id !== keepId) await remove(db, id);
    }
    (globalThis.Nectar ||= {}).DiscStore = {open, read, write, remove, active, commit, clean};
})();
