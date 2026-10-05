const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const cp = require('node:child_process');
const DiscFiles = require('../disc-files.js');
const {prepare} = require('../prepare_disc.cjs');
function fixture(region = 'GPIE01', revision = 1) {
    const bytes = Buffer.alloc(4 * 1024 * 1024);
    bytes.write(region); bytes[7] = revision;
    bytes.writeUInt32BE(0xc2339f3d, 0x1c);
    const entries = ['GPVP01','GPVE01'].includes(region) ? [[1,'',0,8], [1,'AudioRes',0,4], [0,'PSound.aaf',0x10000,5], [0,'test.bin',0x20000,4096], [1,'user',0,6], [0,'empty.bin',0x30000,0], [1,'movie',0,8], [0,'test.thp',0x40000,8192]] : [[1,'',0,8], [1,'dataDir',0,6], [1,'parms',1,4],
        [0,'gamePrms.bin',0x10000,5], [0,'test.bin',0x20000,4096],
        [0,'empty.bin',0x30000,0], [1,'MovieData',0,8], [0,'test.h4m',0x40000,8192]];
    const strings = []; let position = 0;
    entries.forEach(([type,name,offset,size], index) => {
        const p = 0x500 + index * 12;
        bytes.writeUInt32BE(type * 0x1000000 + position, p);
        bytes.writeUInt32BE(offset,p+4); bytes.writeUInt32BE(size,p+8);
        const string = Buffer.from(name+'\0'); strings.push(string); position += string.length;
    });
    Buffer.concat(strings).copy(bytes,0x500+entries.length*12);
    bytes.writeUInt32BE(0x500,0x424); bytes.writeUInt32BE(entries.length*12+position,0x428);
    Buffer.from([1,2,3,4,5]).copy(bytes,0x10000);
    for (let i=0;i<8192;++i) bytes[0x40000+i]=(i*31+i%17)%256;
    for (let i=0;i<4096;++i) bytes[0x20000+i]=i%251;
    return bytes;
}
async function run(decoderDirectory, output) {
    fs.mkdirSync(output,{recursive:true});
    const bytes = fixture();
    const scan = b => DiscFiles.scan((p,n)=>b.subarray(p,p+n),b.length);
    assert.equal(scan(bytes).key,'GPIE01:1');
    assert.equal(scan(fixture('GPIP01',0)).key,'GPIP01:0');
    const p2 = fixture('GPVP01',0);
    assert.equal(scan(p2).key,'GPVP01:0');
    assert.deepEqual(scan(p2).files.map(file=>file.path), ['AudioRes/PSound.aaf','AudioRes/test.bin','user/empty.bin','movie/test.thp']);
    assert.equal(scan(fixture('GPVE01',0)).key, 'GPVE01:0');
    assert.throws(()=>scan(fixture('GPVE01',1)), /unsupported disc/);
    const p2Iso = path.join(output,'synthetic-pikmin2.iso');
    fs.writeFileSync(p2Iso,p2);
    const p2Inputs = [p2Iso];
    assert.throws(()=>scan(fixture('GPIE01',0)),/unsupported disc/);
    const bad = Buffer.from(bytes); bad.writeUInt32BE(bytes.length,0x500+3*12+4);
    assert.throws(()=>scan(bad),/outside the image/);
    assert(!DiscFiles.safePath('dataDir/../bad'));
    const iso = path.join(output,'synthetic.iso'); fs.writeFileSync(iso,bytes);
    fs.writeFileSync(path.join(output,'synthetic-pal.iso'),fixture('GPIP01',0));
    const ciso = path.join(output, 'synthetic.ciso');
    const cisoHeader = Buffer.alloc(0x8000);
    cisoHeader.write('CISO'); cisoHeader.writeUInt32LE(0x200000, 4);
    cisoHeader[8] = cisoHeader[9] = 1;
    fs.writeFileSync(ciso, Buffer.concat([cisoHeader, bytes]));
    const tgc = path.join(output, 'synthetic.tgc');
    const tgcDisc = Buffer.from(bytes);
    for (const file of scan(bytes).files) {
        const location = file.offset + 0x100000;
        bytes.copy(tgcDisc, location, file.offset, file.offset + file.size);
    }
    for (const index of [3,4,5,7]) {
        const field = 0x500 + index * 12 + 4;
        tgcDisc.writeUInt32BE(bytes.readUInt32BE(field) + 0x100000, field);
    }
    const tgcHeader = Buffer.alloc(0x8000);
    tgcHeader.writeUInt32BE(0xae0f38a2, 0);
    tgcHeader.writeUInt32BE(0x8000, 8);
    tgcHeader.writeUInt32BE(0x100000, 12);
    tgcHeader.writeUInt32BE(0x8500, 16);
    tgcHeader.writeUInt32BE(bytes.readUInt32BE(0x428), 20);
    tgcHeader.writeUInt32BE(0x108000, 36);
    tgcHeader.writeUInt32BE(0x300000, 40);
    tgcHeader.writeUInt32BE(0x100000, 52);
    fs.writeFileSync(tgc, Buffer.concat([tgcHeader, tgcDisc]));
    const inputs = [iso, ciso, tgc];
    const dolphin = '/usr/games/dolphin-tool';
    if (fs.existsSync(dolphin)) {
        const p2Rvz = path.join(output,'synthetic-pikmin2-zstd.rvz');
        cp.execFileSync(dolphin,['convert','-i',p2Iso,'-o',p2Rvz,'-f','rvz','-b','131072','-c','zstd','-l','5'],{stdio:'pipe'});
        p2Inputs.push(p2Rvz);
        for (const [format,compression] of [['rvz','zstd'],['rvz','bzip2'],['rvz','lzma2'],['wia','lzma'],['gcz',null]]) {
            const target = path.join(output,`synthetic-${compression || 'zlib'}.${format}`);
            const args = ['convert','-i',iso,'-o',target,'-f',format,'-b',format==='wia'?'2097152':'131072'];
            if (compression) args.push('-c',compression,'-l','5');
            cp.execFileSync(dolphin,args,{stdio:'pipe'}); inputs.push(target);
        }
    }
    for (const input of p2Inputs) {
        const root = await prepare(input,decoderDirectory,path.join(output,'cache'));
        for (const file of scan(p2).files) {
            assert.deepEqual(fs.readFileSync(path.join(root,file.path)),p2.subarray(file.offset,file.offset+file.size));
        }
        assert.equal(await prepare(input,decoderDirectory,path.join(output,'cache')),root);
        console.log('disc import passed: '+path.basename(input));
    }
    for (const input of inputs) {
        const data = await prepare(input,decoderDirectory,path.join(output,'cache'));
        for (const file of scan(bytes).files) {
            assert.deepEqual(fs.readFileSync(path.join(path.dirname(data),file.path)),bytes.subarray(file.offset,file.offset+file.size));
        }
        assert.equal(await prepare(input,decoderDirectory,path.join(output,'cache')),data);
        console.log('disc import passed: '+path.basename(input));
    }
    assert.deepEqual(fs.readFileSync(iso),bytes);
}
if (require.main === module) run(path.resolve(process.argv[2]),path.resolve(process.argv[3])).catch(error=>{console.error(error);process.exitCode=1;});
module.exports={fixture};
