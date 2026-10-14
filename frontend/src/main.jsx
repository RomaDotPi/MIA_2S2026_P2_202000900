import React,{useEffect,useRef,useState} from 'react';
import {createRoot} from 'react-dom/client';
import './style.css';
import './confirmation.css';

const API_BASE=(import.meta.env.VITE_API_BASE_URL||'http://localhost:8080').replace(/\/$/,'');
const clean=value=>String(value??'').replace(/["\\\n\r]/g,'');
const bytes=value=>{if(value<1024)return`${value} B`;if(value<1024**2)return`${(value/1024).toFixed(1)} KB`;return`${(value/1024**2).toFixed(1)} MB`;};

async function request(path,options={}){
  const response=await fetch(`${API_BASE}${path}`,options);
  const data=await response.json();
  if(!response.ok)throw new Error(data.error||`HTTP ${response.status}`);
  return data;
}

function App(){
  const [tab,setTab]=useState('terminal');
  const [state,setState]=useState({session:{active:false},mounts:[],disks:[]});
  const [commands,setCommands]=useState(`# Proyecto 2 - EXT3\nmkdisk -size=10 -unit=M -path=/tmp/disco1.mia\nfdisk -size=5 -unit=M -type=P -path=/tmp/disco1.mia -name=Part1\nmount -path=/tmp/disco1.mia -name=Part1\nmounted`);
  const [output,setOutput]=useState('');
  const [busy,setBusy]=useState(false);
  const [login,setLogin]=useState({id:'',user:'root',pass:'123'});
  const [notice,setNotice]=useState('');
  const [pendingDelete,setPendingDelete]=useState('');
  const [explorer,setExplorer]=useState(null);
  const [journal,setJournal]=useState(null);
  const [journalId,setJournalId]=useState('');
  const fileInput=useRef();

  async function refreshState(){
    try{
      const next=await request('/api/state');
      setState(next);
      const preferred=next.session?.mountId||next.mounts?.[0]?.id||'';
      setLogin(current=>({...current,id:current.id||preferred}));
      setJournalId(current=>next.session?.mountId||current||preferred);
      return next;
    }catch(error){setNotice(`No se pudo consultar el backend: ${error.message}`);return null;}
  }

  async function execute(text,{show=true}={}){
    const data=await request('/api/execute',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({commands:text})});
    if(show)setOutput(data.output||'Sin respuesta');
    await refreshState();
    return data.output||'';
  }

  async function run(){setBusy(true);setNotice('');setPendingDelete('');try{const result=await execute(commands);if(result.includes('CONFIRMACION_REQUERIDA')){const line=commands.split('\n').find(value=>/^\s*fdisk\b/i.test(value)&&/-delete\s*=/i.test(value)&&!/-confirm\b/i.test(value));if(line){setPendingDelete(line);setNotice('La eliminación de la partición necesita confirmación.');}}if(/OK: pérdida simulada/i.test(result))setExplorer(null);else if(state.session?.active&&explorer?.path)await loadExplorer(explorer.path);}catch(error){setOutput(`ERROR: ${error.message}`);}finally{setBusy(false);}}
  async function confirmDelete(){if(!pendingDelete)return;setBusy(true);try{const result=await execute(`${pendingDelete} -confirm`,{show:false});setOutput(previous=>`${previous}\n${result}`);setNotice(result.trim());setPendingDelete('');}catch(error){setNotice(`ERROR: ${error.message}`);}finally{setBusy(false);}}
  async function signIn(event){event.preventDefault();setBusy(true);try{const result=await execute(`login -user="${clean(login.user)}" -pass="${clean(login.pass)}" -id="${clean(login.id)}"`);setNotice(result.trim());const next=await refreshState();if(next?.session?.active){setTab('explorer');await loadExplorer('/');}}catch(error){setNotice(`ERROR: ${error.message}`);}finally{setBusy(false);}}
  async function signOut(){setBusy(true);try{const result=await execute('logout');setNotice(result.trim());setExplorer(null);}catch(error){setNotice(`ERROR: ${error.message}`);}finally{setBusy(false);}}
  async function loadExplorer(path='/'){setBusy(true);try{const data=await request('/api/explorer',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({path})});setExplorer(data);if(!data.ok)setNotice(data.error);}catch(error){setNotice(`ERROR: ${error.message}`);}finally{setBusy(false);}}
  async function loadJournal(id=journalId){if(!id)return;setJournalId(id);setBusy(true);try{const data=await request('/api/journal',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({id})});setJournal(data);if(!data.ok)setNotice(data.error);}catch(error){setNotice(`ERROR: ${error.message}`);}finally{setBusy(false);}}
  function loadScript(event){const file=event.target.files?.[0];if(!file)return;const reader=new FileReader();reader.onload=()=>setCommands(String(reader.result));reader.readAsText(file);}
  function openTab(next){setTab(next);if(next==='explorer'&&state.session?.active&&!explorer)loadExplorer('/');if(next==='journal')loadJournal();}

  useEffect(()=>{refreshState();},[]);

  const parentPath=explorer?.path&&explorer.path!=='/'?(explorer.path.split('/').slice(0,-1).join('/')||'/'):'/';
  return <div className="app-shell">
    <header className="topbar">
      <div className="brand"><div className="logo">efs</div><div><h1>ExtreamFS</h1><p>Administrador EXT2 / EXT3</p></div></div>
      <div className="connection"><span className="dot"/><span>API {API_BASE}</span></div>
    </header>

    <section className="session-card">
      {state.session?.active?<><div><span className="eyebrow">Sesión activa</span><strong>{state.session.user}</strong><small>{state.session.group} · {state.session.mountId}</small></div><button onClick={signOut} disabled={busy}>Cerrar sesión</button></>:
      <form onSubmit={signIn}><div><span className="eyebrow">Iniciar sesión</span><strong>Acceso al sistema de archivos</strong></div><input aria-label="ID de partición" placeholder="ID (001A)" value={login.id} onChange={e=>setLogin({...login,id:e.target.value})}/><input aria-label="Usuario" placeholder="Usuario" value={login.user} onChange={e=>setLogin({...login,user:e.target.value})}/><input aria-label="Contraseña" placeholder="Contraseña" type="password" value={login.pass} onChange={e=>setLogin({...login,pass:e.target.value})}/><button className="primary" disabled={busy}>Ingresar</button></form>}
    </section>

    {notice&&<div className="notice"><span>{notice}</span><div>{pendingDelete&&<button className="danger" onClick={confirmDelete} disabled={busy}>Confirmar eliminación</button>}<button aria-label="Cerrar aviso" onClick={()=>{setNotice('');setPendingDelete('');}}>×</button></div></div>}

    <nav className="tabs">
      {[['terminal','Terminal'],['disks','Discos'],['explorer','Explorador'],['journal','Journal']].map(([id,label])=><button key={id} className={tab===id?'active':''} onClick={()=>openTab(id)}>{label}</button>)}
    </nav>

    {tab==='terminal'&&<section className="workspace terminal-view">
      <div className="toolbar"><label className="file-button">Cargar .smia<input ref={fileInput} type="file" accept=".smia,.txt" onChange={loadScript}/></label><button className="primary" onClick={run} disabled={busy}>{busy?'Ejecutando…':'Ejecutar comandos'}</button><button onClick={()=>{setCommands('');setOutput('');}}>Limpiar</button></div>
      <div className="panes"><article><h2>Entrada</h2><textarea value={commands} onChange={e=>setCommands(e.target.value)} spellCheck="false"/></article><article><h2>Salida</h2><textarea className="output" value={output} readOnly spellCheck="false"/></article></div>
    </section>}

    {tab==='disks'&&<section className="workspace"><div className="section-heading"><div><span className="eyebrow">Inventario</span><h2>Discos y particiones</h2></div><button onClick={refreshState}>Actualizar</button></div>{state.disks?.length?<div className="disk-grid">{state.disks.map(disk=><article className="disk-card" key={disk.path}><div className="disk-title"><div><strong>{disk.path.split('/').pop()}</strong><small>{disk.path}</small></div><span>{bytes(disk.size)}</span></div><dl><div><dt>Fit</dt><dd>{disk.fit}</dd></div><div><dt>Creado</dt><dd>{disk.created}</dd></div></dl><div className="partition-list">{disk.partitions.map(partition=><div className="partition" key={partition.name}><div><strong>{partition.name}</strong><small>{partition.type==='P'?'Primaria':partition.type==='E'?'Extendida':'Lógica'} · Fit {partition.fit}</small></div><div className="partition-meta"><span>{bytes(partition.size)}</span><span className={partition.mounted?'status mounted':'status'}>{partition.mounted?partition.id:'Desmontada'}</span></div></div>)}</div></article>)}</div>:<Empty text="Ejecuta MKDISK y FDISK desde la terminal para comenzar."/>}</section>}

    {tab==='explorer'&&<section className="workspace"><div className="section-heading"><div><span className="eyebrow">Solo lectura</span><h2>Explorador de archivos</h2></div><button onClick={()=>loadExplorer(explorer?.path||'/')} disabled={!state.session?.active}>Actualizar</button></div>{!state.session?.active?<Empty text="Inicia sesión para explorar la partición."/>:explorer?.ok?<><div className="breadcrumb"><button disabled={explorer.path==='/'||explorer.kind==='file'&&explorer.path==='/' } onClick={()=>loadExplorer(parentPath)}>←</button><span>{explorer.path}</span></div>{explorer.kind==='file'?<article className="file-preview"><div><strong>{explorer.path.split('/').pop()}</strong><span>{bytes(explorer.size)} · {explorer.permissions}</span></div><pre>{explorer.content}</pre></article>:<div className="file-table"><div className="file-row header"><span>Nombre</span><span>Permisos</span><span>Propietario</span><span>Tamaño</span><span>Modificado</span></div>{explorer.entries.map(entry=><button className="file-row" key={entry.path} onClick={()=>entry.readable&&loadExplorer(entry.path)} disabled={!entry.readable}><span className="file-name"><i>{entry.type==='directory'?'▣':'▤'}</i>{entry.name}</span><span>{entry.permissions}</span><span>{entry.owner}:{entry.group}</span><span>{bytes(entry.size)}</span><span>{entry.modified}</span></button>)}{!explorer.entries.length&&<Empty text="Esta carpeta está vacía."/>}</div>}</>:<Empty text={explorer?.error||'Abre la raíz para comenzar.'} action={()=>loadExplorer('/')} actionLabel="Abrir raíz"/>}</section>}

    {tab==='journal'&&<section className="workspace"><div className="section-heading"><div><span className="eyebrow">EXT3</span><h2>Journal de operaciones</h2></div><div className="journal-actions"><select value={journalId} onChange={e=>{setJournalId(e.target.value);loadJournal(e.target.value);}}><option value="">Seleccionar partición</option>{state.mounts.map(mount=><option key={mount.id} value={mount.id}>{mount.id} · {mount.name}</option>)}</select><button onClick={()=>loadJournal()}>Actualizar</button></div></div>{journal?.ok?<><div className="journal-summary"><strong>{journal.count}</strong><span>de {journal.capacity} entradas utilizadas</span></div><div className="journal-table"><div className="journal-row header"><span>Operación</span><span>Ruta</span><span>Contenido</span><span>Fecha</span></div>{journal.entries.map((entry,index)=><div className="journal-row" key={`${entry.date}-${index}`}><strong>{entry.operation}</strong><code>{entry.path}</code><span title={entry.content}>{entry.content}</span><time>{entry.date}</time></div>)}</div></>:<Empty text={journal?.error||'Selecciona una partición EXT3 montada.'}/>}</section>}
  </div>;
}

function Empty({text,action,actionLabel}){return <div className="empty"><div>◇</div><p>{text}</p>{action&&<button onClick={action}>{actionLabel}</button>}</div>;}

createRoot(document.getElementById('root')).render(<App/>);
