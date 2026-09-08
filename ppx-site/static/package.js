const configuredRegistry=(localStorage.getItem('ppxRegistry')||'').trim();
const box=document.querySelector('#package');
const parameters=new URLSearchParams(location.search);
const name=parameters.get('name')||'';
const preferLive=parameters.get('source')==='live'&&configuredRegistry;
const escapeHtml=value=>String(value??'').replace(/[&<>"']/g,character=>({
  '&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'
}[character]));

async function bundledPackage(){
  const response=await fetch('static/catalog.json');
  if(!response.ok) throw new Error(`catalog ${response.status}`);
  const data=await response.json();
  return (data.packages||[]).find(item=>item.name===name);
}

async function livePackage(){
  const response=await fetch(`${configuredRegistry}/api/v1/packages/${encodeURIComponent(name)}`);
  if(!response.ok) throw new Error(`${response.status} ${response.statusText}`);
  return response.json();
}

function render(item,source){
  if(!item){
    box.innerHTML='<p class="eyebrow">Package</p><h1>Not found</h1><p>This package is not in the bundled beta catalog.</p>';
    return;
  }
  const versions=item.versions||[{version:item.latest_version||item.version,yanked:false}];
  box.innerHTML=`<p class="eyebrow">${source==='live'?'Live registry':'Bundled package'}</p><h1>${escapeHtml(item.name||name)}</h1><p class="lede">${escapeHtml(item.description||'PunPun package')}</p><div class="install"><span>Install</span><code>ppx add ${escapeHtml(item.name||name)}</code><button id="copy-install">Copy</button></div><p class="meta">Maintained by ${escapeHtml(item.owner||'PunPun Project')} · ${source==='bundled'?'Included with the SDK':'Registry package'}</p><h2>Versions</h2>${versions.map(version=>`<div class="version"><strong>${escapeHtml(version.version)}</strong><span>${version.yanked?'yanked':'available'}</span><span>${version.bundled||source==='bundled'?'SDK bundle':'registry'}</span></div>`).join('')||'<p>No versions published.</p>'}`;
  document.querySelector('#copy-install')?.addEventListener('click',async event=>{
    await navigator.clipboard.writeText(`ppx add ${item.name||name}`);
    event.currentTarget.textContent='Copied';
  });
}

(async()=>{
  if(preferLive){
    try{render(await livePackage(),'live');return;}catch(error){/* use the bundled catalog */}
  }
  try{render(await bundledPackage(),'bundled');}
  catch(error){box.innerHTML='<h1>Catalog unavailable</h1><p>Return to package search and try again.</p>';}
})();
