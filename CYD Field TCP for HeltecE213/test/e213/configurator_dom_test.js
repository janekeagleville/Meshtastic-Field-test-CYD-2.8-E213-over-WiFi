const fs=require('fs');
const html=fs.readFileSync(process.argv[2],'utf8');const script=html.match(/<script>([\s\S]*)<\/script>/)[1];
const ids=[...html.matchAll(/id="(\w+)"/g)].map(m=>m[1]);
const els={};function mk(id){const e={id,value:'0',checked:false,hidden:false,disabled:false,textContent:'',className:'',innerHTML:'',children:[],listeners:{},
 appendChild(c){this.children.push(c);},addEventListener(t,f){this.listeners[t]=f;},set onclick(f){this._c=f;},get onclick(){return this._c;}};return e;}
ids.forEach(i=>els[i]=mk(i));
global.document={getElementById:i=>els[i],createElement:()=>({...mk('x'),children:[{},{}]})};
const opts=fs.readFileSync(process.argv[3],'utf8'),stat=fs.readFileSync(process.argv[4],'utf8');
let written=[],cfg=new Uint8Array([2,0,1,0,0,2,1]),nameVal='OLD',nameWritten=[],timeWritten=[],posVal='52.229700,21.012200,110',posWritten=[],authOk=false,pin='';const lis={};
const dv=u8=>new DataView(u8.buffer,u8.byteOffset,u8.byteLength);
const chars={
 '7a1d0002-5e4b-4c8e-9c5a-543139307031':{readValue:async()=>dv(cfg),writeValue:async b=>{if(authOk){cfg=new Uint8Array(b);written.push(Array.from(b));}},startNotifications:async()=>{},addEventListener(t,f){lis[t]=f;}},
 '7a1d0003-5e4b-4c8e-9c5a-543139307031':{writeValue:async b=>{authOk=new TextDecoder().decode(b)==='123456';},readValue:async()=>dv(new Uint8Array([authOk?1:0]))},
 '7a1d0004-5e4b-4c8e-9c5a-543139307031':{readValue:async()=>dv(new TextEncoder().encode(stat))},
 '7a1d0005-5e4b-4c8e-9c5a-543139307031':{readValue:async()=>dv(new TextEncoder().encode(opts))},
 '7a1d0007-5e4b-4c8e-9c5a-543139307031':{readValue:async()=>dv(new TextEncoder().encode(posVal)),writeValue:async b=>{if(authOk){posVal=new TextDecoder().decode(b);posWritten.push(posVal);}}},
 '7a1d0008-5e4b-4c8e-9c5a-543139307031':{writeValue:async b=>{if(authOk)timeWritten.push(new TextDecoder().decode(b));}},
 '7a1d0006-5e4b-4c8e-9c5a-543139307031':{readValue:async()=>dv(new TextEncoder().encode(nameVal)),writeValue:async b=>{if(authOk){nameVal=new TextDecoder().decode(b);nameWritten.push(nameVal);}}}};
const device={gatt:{connected:true,connect:async()=>({getPrimaryService:async()=>({getCharacteristic:async u=>chars[u]})}),disconnect(){}},addEventListener(){}};
Object.defineProperty(global,'navigator',{value:{bluetooth:{requestDevice:async()=>device}},configurable:true});global.TextDecoder=TextDecoder;global.TextEncoder=TextEncoder;
global.setInterval=()=>1;global.clearInterval=()=>{};global.setTimeout=(f)=>0;
(async()=>{const fn=new Function(script+';return {connect,authorize,apply};');const api=fn();
 await api.connect();const a=require('assert');console.log('msg:',els.msg.textContent);
 a.strictEqual(els.conPill.textContent,'connected');a.strictEqual(els.cCfg.hidden,false);a.strictEqual(els.meshPreset.value,'0');a.strictEqual(els.nodeName.value,'OLD');a.strictEqual(els.lat.value,'52.229700');a.strictEqual(els.lon.value,'21.012200');a.strictEqual(els.alt.value,'110');a.strictEqual(els.beacon.value,'2');a.strictEqual(els.autoPing.checked,true);a.strictEqual(els.invert.checked,true);
 a.strictEqual(els.bApply.disabled,true);
 els.pin.value='000000';await api.authorize();a.strictEqual(els.authPill.textContent,'not authorized');
 els.pin.value='123456';await api.authorize();a.strictEqual(els.authPill.textContent,'authorized');a.strictEqual(els.bApply.disabled,false);
 a.strictEqual(els.bClock.disabled,false);await els.bClock.onclick();a.strictEqual(timeWritten.length,1);a.ok(/^\d{10}$/.test(timeWritten[0]));
 els.meshPreset.value='7';els.nodeName.value='  DACHA-1 łódź-long-name';els.flip.checked=true;els.beacon.value='3';els.autoPing.checked=false;els.lat.value='50,0614';els.lon.value='19.9383';els.alt.value='';await api.apply();
 a.deepStrictEqual(written[0],[2,7,1,1,0,3,0]);a.deepStrictEqual(posWritten,['50.061400,19.938300,0']);a.deepStrictEqual(nameWritten,['DACHA-1 d-lo']);els.lat.value='';els.lon.value='';els.alt.value='';await api.apply();a.deepStrictEqual(posWritten[1],' ');els.lat.value='95';els.lon.value='1';await api.apply();a.strictEqual(posWritten.length,2);a.ok(/Position/.test(els.msg.textContent));console.log('dom OK');})().catch(e=>{console.error(e);process.exit(1);});
