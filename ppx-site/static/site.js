const configuredRegistry=(localStorage.getItem('ppxRegistry')||'').trim();
const results=document.querySelector('#results');
const status=document.querySelector('#status');
const form=document.querySelector('#search');
const query=document.querySelector('#q');

function escapeHtml(value){
  return String(value??'').replace(/[&<>"']/g,character=>({
    '&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'
  }[character]));
}

async function bundledPackages(term){
  const response=await fetch('static/catalog.json');
  if(!response.ok) throw new Error(`catalog ${response.status}`);
  const data=await response.json();
  const needle=term.trim().toLowerCase();
  return (data.packages||[]).filter(item=>
    !needle||item.name.toLowerCase().includes(needle)||(item.description||'').toLowerCase().includes(needle)
  );
}

async function livePackages(term){
  const response=await fetch(`${configuredRegistry}/api/v1/search?q=${encodeURIComponent(term)}`);
  if(!response.ok) throw new Error(`${response.status} ${response.statusText}`);
  const data=await response.json();
  return data.packages||data.results||data||[];
}

function render(packages,source,note=''){
  results.replaceChildren();
  status.textContent=`${packages.length} ${source} package${packages.length===1?'':'s'}${note}`;
  if(!packages.length){
    results.innerHTML='<div class="empty"><h3>No packages found</h3><p>Try a broader name or description.</p></div>';
    return;
  }
  for(const item of packages){
    const card=document.createElement('a');
    card.className='card';
    card.href=`package.html?name=${encodeURIComponent(item.name)}${source==='live'?'&source=live':''}`;
    card.innerHTML=`<div class="card-top"><h3>${escapeHtml(item.name)}</h3><span class="pill">${source==='bundled'?'included':'registry'}</span></div><p>${escapeHtml(item.description||'PunPun package')}</p><div class="meta"><code>ppx add ${escapeHtml(item.name)}</code><span>v${escapeHtml(item.latest_version||item.latest||item.version||'beta')}</span></div>`;
    results.append(card);
  }
}

async function search(term=''){
  status.textContent='Searching…';
  results.replaceChildren();
  if(configuredRegistry){
    try{
      render(await livePackages(term),'live');
      return;
    }catch(error){
      render(await bundledPackages(term),'bundled',' · live registry fallback');
      return;
    }
  }
  render(await bundledPackages(term),'bundled');
}

form.addEventListener('submit',event=>{event.preventDefault();search(query.value)});
search();
