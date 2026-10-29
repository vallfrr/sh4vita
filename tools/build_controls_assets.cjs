// Deterministic packing of existing vector/PNG artwork; no game files required.
const fs = require('fs'), path = require('path'), sharp = require('sharp');
const root = path.resolve(__dirname, '..', 'port', 'assets', 'controls');
const files = ['cross','circle','square','triangle','l','r','start','select','left_stick','right_stick','dpad'].map(n=>'kenney_'+n+'.svg');
const uri = name => 'data:'+(name.endsWith('.svg')?'image/svg+xml':'image/png')+';base64,'+fs.readFileSync(path.join(root,name)).toString('base64');
(async()=>{
 const images = [`<image href="${uri('vita-outline.svg')}" x="0" y="0" width="1024" height="465.4545"/>`];
 files.forEach((f,i)=>images.push(`<image href="${uri(f)}" x="${i*64+8}" y="464" width="48" height="48"/>`));
 const svg=`<svg xmlns="http://www.w3.org/2000/svg" width="1024" height="512">${images.join('')}</svg>`;
 const {data,info}=await sharp(Buffer.from(svg)).ensureAlpha().raw().toBuffer({resolveWithObject:true});
 const head=Buffer.alloc(18);head[2]=2;head.writeUInt16LE(info.width,12);head.writeUInt16LE(info.height,14);head[16]=32;head[17]=0x28;
 for(let i=0;i<data.length;i+=4){const r=data[i];data[i]=data[i+2];data[i+2]=r;}
 fs.writeFileSync(path.join(root,'controls.tga'),Buffer.concat([head,data]));
 process.stdout.write(`Packed ${files.length} prompts + outline into ${info.width}x${info.height} BGRA atlas\n`);
})();
