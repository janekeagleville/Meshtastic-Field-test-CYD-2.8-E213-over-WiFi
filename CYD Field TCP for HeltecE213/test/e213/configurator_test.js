// Test logiki konfiguratora (bez przegladarki): node test/configurator_test.js
const fs=require('fs');const assert=require('assert');
const html=fs.readFileSync(__dirname+'/../../tools/e213/configurator.html','utf8');
const code=html.split('//LOGIC-BEGIN')[1].split('//LOGIC-END')[0];
const m={};new Function('m',code+';m.UUID=UUID;m.FIELDS=FIELDS;m.decodeCfg=decodeCfg;m.encodeCfg=encodeCfg;m.parseOptions=parseOptions;m.validCfg=validCfg;m.presetInfo=presetInfo;m.statRows=statRows;m.cleanName=cleanName;m.posToText=posToText;m.textToPos=textToPos;m.clockText=clockText;m.localEpoch=localEpoch;')(m);
const opt=m.parseOptions(fs.readFileSync(process.argv[2]||'json_options.txt','utf8'));
assert.strictEqual(opt.presets.length,11);assert.strictEqual(opt.presets[3].name,'MediumFast');assert.strictEqual(opt.presets[3].sf,9);assert.strictEqual(opt.powers.length,5);assert.deepStrictEqual(opt.beacons,[0,15,30,60,180]);
const c={power:4,meshPreset:9,invert:0,flip:1,pwrHigh:0,beacon:3,autoPing:1};
const b=m.encodeCfg(c);assert.deepStrictEqual(Array.from(b),[4,9,0,1,0,3,1]);assert.deepStrictEqual(m.decodeCfg(b),c);assert.strictEqual(m.decodeCfg(new Uint8Array(3)),null);
assert.ok(m.validCfg(c,opt));assert.ok(!m.validCfg({...c,meshPreset:11},opt));assert.ok(!m.validCfg({...c,power:5},opt));assert.ok(!m.validCfg({...c,invert:-1},opt));assert.ok(!m.validCfg({...c,power:1.5},opt));assert.ok(!m.validCfg({...c,beacon:5},opt));assert.ok(!m.validCfg({...c,autoPing:2},opt));assert.ok(m.validCfg({...c,pwrHigh:3},opt));assert.ok(!m.validCfg({...c,pwrHigh:4},opt));
assert.strictEqual(m.posToText('52.2297','21,0122','110'),'52.229700,21.012200,110');assert.strictEqual(m.posToText('-33.5','-70.25',''),'-33.500000,-70.250000,0');assert.strictEqual(m.posToText('','',''),'');
for(const bad of [['91','0','0'],['10','181','0'],['abc','1','1'],['10','','5'],['10','20','1.5'],['10','20','99999'],['','20','']])assert.strictEqual(m.posToText(...bad),null,JSON.stringify(bad));
assert.deepStrictEqual(m.textToPos('52.229700,21.012200,110'),{lat:'52.229700',lon:'21.012200',alt:'110'});assert.deepStrictEqual(m.textToPos(''),{lat:'',lon:'',alt:''});
assert.strictEqual(m.cleanName('  DACHA-1 '),'DACHA-1');assert.strictEqual(m.cleanName('Za\u0142\u0105czniczek-bardzo-dlugi'),'Zaczniczek-b');
assert.throws(()=>m.parseOptions('{"x":1}'));
const st=JSON.parse(fs.readFileSync(process.argv[3]||'json_status.txt','utf8'));const rows=m.statRows(st);assert.ok(rows.length>=10&&rows.every(r=>typeof r.label==='string'&&typeof r.value==='string'&&!/undefined|NaN/.test(r.value)));
console.log(m.presetInfo(opt.presets[7]));
// UUID zgodne z firmware
const cpp=fs.readFileSync(__dirname+'/../../src/e213/main.cpp','utf8');for(const [k,u] of Object.entries(m.UUID))assert.ok(cpp.includes('"'+u+'"'),'UUID '+k+' nie wystepuje w main.cpp');
console.log('configurator OK');

assert.strictEqual(m.clockText(0),'not set');assert.strictEqual(m.clockText(1761404700),'2025-10-25 15:05');
// local epoch = UTC epoch shifted by the time-zone offset (the board treats it as local time)
assert.strictEqual(m.localEpoch(new Date(Date.UTC(2025,9,25,13,5,0))),Math.floor(Date.UTC(2025,9,25,13,5,0)/1000-new Date(Date.UTC(2025,9,25,13,5,0)).getTimezoneOffset()*60));
