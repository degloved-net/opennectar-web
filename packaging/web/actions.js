(() => {
    class SaveArchive {
        static maxBytes = 40 * 1024 * 1024;
        static maxTextLength = 56 * 1024 * 1024;

        static isAllowedPath(path) {
            if (typeof path !== 'string' || path.length > 512 || /[\\\x00-\x1f:]/.test(path)) {
                return false;
            }
            const parts = path.split('/');
            if (parts.some(part => !part || part === '.' || part === '..')) {
                return false;
            }
            return path === 'pikmin_settings.conf' || (parts[0] === 'save' && parts.length > 1);
        }

        static encodeBytes(bytes) {
            let binary = '';
            for (let offset = 0; offset < bytes.length; offset += 8192) {
                binary += String.fromCharCode(...bytes.subarray(offset, offset + 8192));
            }
            return btoa(binary);
        }

        static decode(text) {
            if (typeof text !== 'string' || text.length > this.maxTextLength) {
                throw new Error('the save backup is too large');
            }
            const archive = JSON.parse(text);
            if ((archive?.game || 'pikmin1') !== (Nectar.game || 'pikmin1'))
                throw new Error('this save backup belongs to the other pikmin game');
            if (archive?.format !== 'open-nectar-save' || archive.version !== 1
                || !Array.isArray(archive.files) || archive.files.length > 4096) {
                throw new Error('this is not a supported open nectar save backup');
            }
            const paths = new Set();
            let total = 0;
            const files = archive.files.map(file => {
                if (!file || !this.isAllowedPath(file.path) || paths.has(file.path)) {
                    throw new Error('the backup contains an unsafe or duplicate path');
                }
                paths.add(file.path);
                if (typeof file.data !== 'string' || file.data.length % 4 !== 0
                    || /[^A-Za-z0-9+/=]/.test(file.data)
                    || file.data.slice(0, -2).includes('=')
                    || (file.data.at(-2) === '=' && file.data.at(-1) !== '=')) {
                    throw new Error('the backup contains invalid file data');
                }
                total += file.data.length / 4 * 3 - (file.data.endsWith('==') ? 2 : file.data.endsWith('=') ? 1 : 0);
                if (total > this.maxBytes) {
                    throw new Error('the save backup is too large');
                }
                const binary = atob(file.data);
                const bytes = Uint8Array.from(binary, character => character.charCodeAt(0));
                return {path: file.path, bytes};
            });
            for (const path of paths) {
                const parts = path.split('/');
                while (parts.length > 1) {
                    parts.pop();
                    if (paths.has(parts.join('/'))) throw new Error('the backup uses a file as a folder');
                }
            }
            return {files, bytes: total};
        }

        static create(files) {
            const archive = {format: 'open-nectar-save', version: 1, created: new Date().toISOString(), files: []};
            if (Nectar.game === 'pikmin2') archive.game = 'pikmin2';
            let total = 0;
            const paths = new Set();
            for (const file of files) {
                if (!this.isAllowedPath(file.path) || paths.has(file.path) || !(file.bytes instanceof Uint8Array)) {
                    throw new Error('a save file could not be backed up safely');
                }
                paths.add(file.path);
                total += file.bytes.length;
                if (total > this.maxBytes || paths.size > 4096) {
                    throw new Error('the save backup is too large');
                }
                archive.files.push({path: file.path, data: this.encodeBytes(file.bytes)});
            }
            return archive;
        }
    }

    class FilePicker {
        constructor(input, notify) {
            this.input = input;
            this.notify = notify;
            this.pending = null;
            window.addEventListener('keydown', event => this.onGesture(event), true);
            window.addEventListener('pointerdown', event => this.onGesture(event), true);
        }

        choose(accept) {
            if (this.pending) {
                return Promise.reject(new Error('a file dialog is already open'));
            }
            this.input.value = '';
            this.input.accept = accept;
            return new Promise((resolve, reject) => {
                const finish = (file, error) => {
                    clearTimeout(this.pending?.timer);
                    this.input.removeEventListener('change', change);
                    this.input.removeEventListener('cancel', cancel);
                    this.pending = null;
                    if (error) reject(error);
                    else resolve(file);
                };
                const change = () => finish(this.input.files[0] || null);
                const cancel = () => finish(null);
                this.pending = {finish, waiting: true};
                this.input.addEventListener('change', change);
                this.input.addEventListener('cancel', cancel);
                this.open();
            });
        }

        open() {
            if (!this.pending?.waiting) return;
            try {
                if (navigator.userActivation && !navigator.userActivation.isActive) {
                    throw new DOMException('a keyboard or mouse action is required', 'NotAllowedError');
                }
                if (this.input.showPicker) this.input.showPicker();
                else this.input.click();
                this.pending.waiting = false;
                clearTimeout(this.pending.timer);
            } catch (error) {
                if (error.name !== 'NotAllowedError') {
                    this.pending.finish(null, error);
                    return;
                }
                this.notify('press enter or click to choose a file. escape cancels.');
                clearTimeout(this.pending.timer);
                this.pending.timer = setTimeout(() => this.pending?.finish(null), 30000);
            }
        }

        onGesture(event) {
            if (!this.pending?.waiting || event.repeat) return;
            if (event.type === 'keydown' && !['Enter', 'Space', 'Escape'].includes(event.code)) return;
            event.preventDefault();
            event.stopImmediatePropagation();
            if (event.code === 'Escape') this.pending.finish(null);
            else this.open();
        }
    }

    class SaveTransfer {
        static async access(mode, action) {
            const database = await new Promise((resolve, reject) => {
                const request = indexedDB.open(Nectar.game === 'pikmin2' ? 'nectar-pikmin2-transfer' : 'open-nectar-transfer', 1);
                request.onupgradeneeded = () => request.result.createObjectStore('pending');
                request.onsuccess = () => resolve(request.result);
                request.onerror = () => reject(request.error);
                request.onblocked = () => reject(new Error('close other nectar tabs before restoring saves'));
            });
            try {
                return await new Promise((resolve, reject) => {
                    const transaction = database.transaction('pending', mode);
                    const request = action(transaction.objectStore('pending'));
                    transaction.oncomplete = () => resolve(request.result);
                    transaction.onabort = () => reject(transaction.error || new Error('save staging failed'));
                    transaction.onerror = () => {};
                });
            } finally {
                database.close();
            }
        }

        static read() {
            return this.access('readonly', store => store.get('restore'));
        }

        static write(backup) {
            return this.access('readwrite', store => store.put(backup, 'restore'));
        }

        static clear() {
            return this.access('readwrite', store => store.delete('restore'));
        }
    }

    class GameFiles {
        constructor(runtime, input, record) {
            this.runtime = runtime;
            this.input = input;
            this.record = record;
            this.busy = false;
            this.library = new Nectar.TextureLibrary(runtime);
            this.picker = new FilePicker(input, message => this.notice(message));
        }

        notice(message, error = false) {
            this.runtime.ccall('nectarNotice', null, ['number', 'string'], [Number(error), String(message)]);
        }

        progress(files, total) {
            this.runtime.ccall('nectarFileProgress', null, ['number', 'string'],
                [files, `installing textures: ${files} / ${total}`]);
        }

        async run(action, operation) {
            if (this.busy) {
                this.notice('finish the current file operation first');
                return;
            }
            this.busy = true;
            try {
                this.runtime.ccall('nectarFileBegin', null, ['number'], [action]);
                if (document.pointerLockElement) document.exitPointerLock();
                const message = await operation();
                this.runtime.ccall('nectarFileResult', null, ['number', 'number', 'string'],
                    [action, message ? 1 : 0, message || 'cancelled. nothing was changed.']);
            } catch (error) {
                const message = String(error.message || error);
                this.record('file', message);
                this.runtime.ccall('nectarFileResult', null, ['number', 'number', 'string'], [action, -1, message]);
            } finally {
                this.input.value = '';
                this.busy = false;
                this.runtime._nectarClearInput();
                this.runtime.canvas.focus({preventScroll: true});
                this.runtime.nectarResumeAudio?.();
            }
        }

        request(action) {
            if (!Number.isInteger(action) || action < 0 || action > 8) return;
            return this.run(action, async () => {
                if (action === 0) {
                    const files = await this.runtime.nectarExportSaves();
                    GameFiles.download(JSON.stringify(Nectar.SaveArchive.create(files)), 'nectar-save-backup.json', 'application/json');
                    return 'save backup downloaded.';
                }
                const file = await this.picker.choose(action === 1 ? '.json,application/json' : '.zip,application/zip');
                if (!file) return null;
                if (action === 1) return this.restore(file);
                if (action === 2) return this.installTextures(file);
                return this.installModel(file, action - 3);
            });
        }

        async installTextures(file) {
            this.notice('checking texture pack...');
            const installed = await this.library.installZip(file, (files, total) => this.progress(files, total));
            await this.library.select(installed.id);
            return `${installed.files} textures installed. pack active.`;
        }

        selectTexture(id) {
            return this.run(2, async () => {
                await this.library.select(id);
                return id ? 'texture pack active.' : 'original textures restored.';
            });
        }

        async installModel(file, kind) {
            this.notice('checking model archive...');
            const archive = await Nectar.PackArchive.open(file);
            for (const entry of archive.entries) {
                const bytes = await archive.read(entry);
                if (entry.name.toLowerCase().endsWith('.png')) {
                    Nectar.TextureLibrary.validateTexture('model.png', bytes);
                }
            }
            this.notice('converting model pack...');
            await new Promise(resolve => setTimeout(resolve, 0));
            await this.runtime.nectarInstallZip(new Uint8Array(await file.arrayBuffer()), kind, file.name);
            return 'model installed. use the in-game restart prompt to apply it.';
        }

        async restore(file) {
            if (file.size > Nectar.SaveArchive.maxTextLength) throw new Error('the save backup is too large');
            const text = await file.text();
            Nectar.SaveArchive.decode(text);
            if (!confirm('restore this backup and restart the game? current saves will be replaced, and unsaved progress will be lost.')) return null;
            await SaveTransfer.write(text);
            this.restarting = true;
            location.reload();
            return 'restarting to restore the backup...';
        }

        static download(data, name, type) {
            const url = URL.createObjectURL(data instanceof Blob ? data : new Blob([data], {type}));
            const link = document.createElement('a');
            link.href = url;
            link.download = name;
            document.body.appendChild(link);
            link.click();
            link.remove();
            setTimeout(() => URL.revokeObjectURL(url), 10000);
        }
    }

    Object.assign(globalThis.Nectar, {GameFiles, SaveTransfer, SaveArchive});
})();
