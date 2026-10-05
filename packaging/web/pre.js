(() => {
    let storageMounted = false;
    let syncPending = null;
    let audioPending = Promise.resolve();
    let audioPaused = false;

    function reportStorage(state, error) {
        if (Module['nectarStorage']) {
            Module['nectarStorage'](state, error ? String(error) : null);
        }
    }

    function syncStorage(populate) {
        return new Promise((resolve, reject) => {
            FS.syncfs(populate, error => error ? reject(error) : resolve());
        });
    }

    function readSaveFiles() {
        const files = [];
        const paths = ['/persistent/save', '/persistent/pikmin_settings.conf'];
        let total = 0;
        while (paths.length) {
            const path = paths.pop();
            if (!FS.analyzePath(path).exists) {
                continue;
            }
            const stat = FS.lstat(path);
            if (FS.isDir(stat.mode)) {
                for (const name of FS.readdir(path).sort()) {
                    if (name !== '.' && name !== '..') {
                        paths.push(path + '/' + name);
                    }
                }
            } else if (FS.isFile(stat.mode)) {
                total += stat.size;
                if (total > 40 * 1024 * 1024 || files.length >= 4096) {
                    throw new Error('the save files are too large to back up');
                }
                files.push({path: path.slice('/persistent/'.length), bytes: FS.readFile(path)});
            } else {
                throw new Error('the save folder contains an unsupported file');
            }
        }
        return files.sort((left, right) => left.path.localeCompare(right.path));
    }

    function replaceSaveFiles(files) {
        Module['nectarRemoveTree']('/persistent/save');
        Module['nectarRemoveTree']('/persistent/pikmin_settings.conf');
        for (const file of files) {
            const path = '/persistent/' + file.path;
            FS.mkdirTree(path.slice(0, path.lastIndexOf('/')));
            FS.writeFile(path, file.bytes);
        }
    }

    async function restoreSaveFiles(backup) {
        const previous = readSaveFiles();
        try {
            replaceSaveFiles(backup.files);
            await syncStorage(false);
        } catch (error) {
            replaceSaveFiles(previous);
            await syncStorage(false);
            throw error;
        }
    }

    Module['nectarRemoveTree'] = path => {
        if (!FS.analyzePath(path).exists) return;
        if (FS.isDir(FS.lstat(path).mode)) {
            for (const name of FS.readdir(path)) {
                if (name !== '.' && name !== '..') Module['nectarRemoveTree'](path + '/' + name);
            }
            FS.rmdir(path);
        } else {
            FS.unlink(path);
        }
    };

    Module['nectarSnapshot'] = () => ({
        memoryBytes: typeof wasmMemory !== 'undefined' ? wasmMemory.buffer.byteLength : null,
        hasGameData: typeof FS !== 'undefined' && (FS.analyzePath('/assets/dataDir/parms/gamePrms.bin').exists || FS.analyzePath('/assets/AudioRes/PSound.aaf').exists),
        persistentEntries: typeof FS !== 'undefined' && FS.analyzePath('/persistent').exists ? FS.readdir('/persistent') : [],
        audioState: Module['SDL2']?.audioContext?.state || 'not created',
        aspectMode: Module['_nectarGetAspectRatioMode'] ? Module['_nectarGetAspectRatioMode']() : null,
        aspectRatio: Module['_nectarGetAspectRatio'] ? Module['_nectarGetAspectRatio']() : null,
        textures: Module['nectarTextureSnapshot']?.() || null
    });

    Module['nectarSyncSaves'] = () => {
        if (!storageMounted) {
            return Promise.reject(new Error('browser storage is unavailable'));
        }
        const operation = (syncPending || Promise.resolve()).catch(() => {}).then(() => syncStorage(false));
        syncPending = operation;
        return operation.catch(error => {
            reportStorage('unavailable', error);
            throw error;
        }).finally(() => {
            if (syncPending === operation) syncPending = null;
        });
    };

    Module['nectarExportSaves'] = async () => {
        try {
            await Module['nectarSyncSaves']();
        } catch (error) {
            if (Module['nectarRecord']) {
                Module['nectarRecord']('storage', 'exporting the in-memory backup: ' + error);
            }
        }
        return readSaveFiles();
    };

    Module['nectarInstallZip'] = async (bytes, kind, name) => {
        if (!storageMounted) {
            throw new Error('persistent storage is required to install packs');
        }
        const safeName = String(name).replace(/[^a-zA-Z0-9._-]/g, '_').replace(/^\.+/, '').slice(0, 120) || 'pack.zip';
        const path = '/tmp/' + safeName;
        FS.writeFile(path, bytes, {canOwn: true});
        try {
            const error = Module['ccall']('nectarInstallPack', 'string', ['number', 'string'], [kind, path]);
            if (error) throw new Error(error);
            await Module['nectarSyncSaves']();
        } finally {
            FS.unlink(path);
        }
    };

    Module['nectarSetAudioPaused'] = paused => {
        audioPaused = !!paused;
        audioPending = audioPending.then(async () => {
            const context = Module['SDL2']?.audioContext;
            if (!context || context.state === 'closed') {
                return;
            }
            if (audioPaused && context.state === 'running') {
                await context.suspend();
            } else if (!audioPaused && context.state === 'suspended') {
                await context.resume();
            }
        }).catch(error => {
            if (Module['nectarRecord']) {
                Module['nectarRecord']('audio', error.message || error);
            }
        });
        return audioPending;
    };

    Module['nectarResumeAudio'] = () => {
        if (!audioPaused && Module['SDL2']?.audioContext?.state === 'suspended') {
            return Module['nectarSetAudioPaused'](false);
        }
    };
    Module['preRun'] = Module['preRun'] || [];
    Module['preRun'].push(() => {
        if (!Module['nectarLoadOwnGame']) return;
        addRunDependency('nectar-own-game');
        Promise.resolve().then(() => Module['nectarLoadOwnGame'](FS)).then(() => {
            removeRunDependency('nectar-own-game');
        }).catch(error => abort('could not load your game copy: ' + (error.message || error)));
    });
    Module['preRun'].push(() => {
        let finished = false;
        addRunDependency('nectar-idbfs');
        reportStorage('mounting');
        const finish = error => {
            if (finished) {
                return;
            }
            finished = true;
            reportStorage(error ? 'unavailable' : 'ready', error);
            if (error && Module['nectarPendingSave']) {
                abort('save restoration failed; your existing save must be checked before starting');
            }
            removeRunDependency('nectar-idbfs');
        };
        try {
            if (Module['nectarPrepareTextures'] && !IDBFS.nectarTexturesExcluded) {
                const excluded = path => path === '/persistent/Load/Textures'
                    || path.startsWith('/persistent/Load/Textures/')
                    || /^\/persistent\/Load\/\.staging-web-[^/]+(?:\/|$)/.test(path);
                for (const method of ['getLocalSet', 'getRemoteSet']) {
                    const original = IDBFS[method];
                    IDBFS[method] = (mount, callback) => original(mount, (error, set) => {
                        if (!error) for (const path of Object.keys(set.entries)) if (excluded(path)) delete set.entries[path];
                        callback(error, set);
                    });
                }
                IDBFS.nectarTexturesExcluded = true;
            }
            FS.mkdirTree('/persistent');
            FS.mount(IDBFS, {autoPersist: true}, '/persistent');
            syncStorage(true).then(async () => {
                storageMounted = true;
                FS.mkdirTree('/persistent/Load');
                for (const name of FS.readdir('/persistent/Load')) {
                    if (/^\.staging-web-[a-z0-9-]+$/.test(name)) {
                        Module['nectarRemoveTree']('/persistent/Load/' + name);
                    }
                }
                if (!FS.analyzePath('/Load').exists) {
                    FS.symlink('/persistent/Load', '/Load');
                }
                if (Module['nectarPrepareTextures']) await Module['nectarPrepareTextures'](FS);
                if (Module['nectarPendingSave']) {
                    await restoreSaveFiles(Module['nectarPendingSave']);
                    if (Module['nectarConsumePendingSave']) await Module['nectarConsumePendingSave']();
                    Module['nectarPendingSave'] = null;
                    if (Module['nectarRecord']) {
                        Module['nectarRecord']('storage', 'save backup restored before engine startup');
                    }
                }
                finish();
            }).catch(error => {
                storageMounted = false;
                finish(error);
            });
        } catch (error) {
            finish(error);
        }
    });
})();
