"use strict";
const $ = id => document.getElementById(id);
const {t, setText: translated, init: initLanguages} = CySynckI18n;
initLanguages();
let token = "", selected = "", transferLoad = 0, gitLoad = 0, gitReviewLoad=0, gitHistoryLoad=0, gitReceiptsLoad=0;
function clearGitReview() {
  $("git-review").replaceChildren(); $("git-review-patch").textContent="";
  ++gitReviewLoad; ++gitHistoryLoad;
}
try { token = sessionStorage.getItem("cysynck-token") || ""; } catch (_) { /* Storage may be disabled. */ }
let storagePlan = null, servicePreview = null;
let lfsCache=null,lfsRecovery=null,lfsHistory=null,lfsGeneration=0,lfsBusy=false,lfsVerify=false;
function resetLfsCache() {
  ++lfsGeneration;lfsCache=lfsRecovery=lfsHistory=null;lfsBusy=false;lfsVerify=false;
  $("lfs-cache-peer").value=$("lfs-cache-branch").value="";$("lfs-cache-source").replaceChildren();renderLfsCache();
}
function lfsInput() {
  const peer=$("lfs-cache-peer").value,branch=$("lfs-cache-branch").value;
  return peer || branch?{peer,branch}:{};
}
function lfsStateLabel(state) {return t(state==="missing"?"Missing":state==="present"?"Present, unverified":state==="verified"?"Verified":state==="corrupt"?"Corrupt":state==="size_mismatch"?"Wrong size":"Unsafe path");}
function renderLfsCache() {
  for(const id of ["lfs-cache-peer","lfs-cache-branch","lfs-cache-source","lfs-cache-inspect","lfs-cache-verify","lfs-cache-history"]) $(id).disabled=lfsBusy;
  $("lfs-cache-prev").disabled=lfsBusy || !lfsCache || lfsCache.offset===0;$("lfs-cache-next").disabled=lfsBusy || !lfsCache?.has_more;
  $("lfs-cache-preview").disabled=lfsBusy || !lfsCache || !$("lfs-cache-source").value;
  $("lfs-cache-apply").disabled=lfsBusy || !lfsRecovery?.can_recover;
  $("lfs-history-prev").disabled=lfsBusy || !lfsHistory || lfsHistory.offset===0;$("lfs-history-next").disabled=lfsBusy || !lfsHistory?.has_more;
  for(const id of ["lfs-cache-summary","lfs-cache-recovery-summary"]) {$(id).removeAttribute("data-i18n");$(id).textContent="";}
  $("lfs-cache-objects").replaceChildren();$("lfs-cache-records").replaceChildren();
  if(lfsCache) {
    translated($("lfs-cache-summary"),"{total} objects · {missing} missing · {damaged} damaged · {verified} verified",lfsCache);
    $("lfs-cache-objects").replaceChildren(...lfsCache.objects.map(object=>text("p",object.sha256+" / "+lfsStateLabel(object.state)+" / "+object.size+" B")));
  }
  if(lfsRecovery) translated($("lfs-cache-recovery-summary"),"{available} available missing objects · {unavailable} unavailable · {bytes} bytes",{available:lfsRecovery.objects.length,unavailable:lfsRecovery.unavailable.length,bytes:lfsRecovery.bytes});
  if(lfsHistory) {
    for(const receipt of lfsHistory.records) $("lfs-cache-records").append(text("p",receipt.created+" / "+t(receipt.status==="complete"?"Complete":"Incomplete")+" / "+t("Installed objects")+": "+receipt.installed+" / "+receipt.source_peer));
    for(const error of lfsHistory.errors) $("lfs-cache-records").append(text("p",error.error));
  }
}
async function lfsOperation(route,input,done) {
  if(!selected || lfsBusy) return;
  const project=selected,generation=++lfsGeneration;lfsBusy=true;renderLfsCache();
  try {
    const result=await api("projects/"+project+"/git-"+route,"POST",input);
    if(project===selected && generation===lfsGeneration) await done(result);
  } catch(error) {if(project===selected && generation===lfsGeneration) lfsRecovery=null;throw error;}
  finally {if(project===selected && generation===lfsGeneration) {lfsBusy=false;renderLfsCache();}}
}
function loadLfsCache(verify=false,offset=0,continuation=false) {
  const input={...lfsInput(),verify,offset,limit:100};
  if(continuation && lfsCache) input.inventory_hash=lfsCache.inventory_hash;
  lfsRecovery=null;
  return lfsOperation("lfs-cache",input,result=>{
    lfsCache=result;lfsVerify=verify;const source=$("lfs-cache-source").value;
    $("lfs-cache-source").replaceChildren(...result.source_peers.map(peer=>{const option=document.createElement("option");option.value=option.textContent=peer;return option;}));
    if(result.source_peers.includes(source)) $("lfs-cache-source").value=source;
  });
}
for(const id of ["lfs-cache-peer","lfs-cache-branch"]) $(id).addEventListener("input",()=>{++lfsGeneration;lfsCache=lfsRecovery=null;renderLfsCache();});
$("lfs-cache-source").addEventListener("change",()=>{++lfsGeneration;lfsRecovery=null;renderLfsCache();});
$("lfs-cache-inspect").addEventListener("click",()=>safe(()=>loadLfsCache(false)));
$("lfs-cache-verify").addEventListener("click",()=>safe(()=>loadLfsCache(true)));
$("lfs-cache-prev").addEventListener("click",()=>safe(()=>loadLfsCache(lfsVerify,Math.max(0,lfsCache.offset-100),true)));
$("lfs-cache-next").addEventListener("click",()=>safe(()=>loadLfsCache(lfsVerify,lfsCache.offset+100,true)));
$("lfs-cache-preview").addEventListener("click",()=>safe(()=>{lfsRecovery=null;return lfsOperation("lfs-recover",{...lfsInput(),source_peer:$("lfs-cache-source").value},result=>{lfsRecovery=result;});}));
$("lfs-cache-apply").addEventListener("click",()=>safe(()=>{
  if(!lfsRecovery?.can_recover) return;
  return lfsOperation("lfs-recover",{...lfsInput(),source_peer:$("lfs-cache-source").value,apply:true,plan:lfsRecovery.plan},async()=>{lfsCache=lfsRecovery=lfsHistory=null;await refresh();});
}));
function loadLfsHistory(offset=0) {return lfsOperation("lfs-recoveries",{offset,limit:20},result=>{lfsHistory=result;});}
$("lfs-cache-history").addEventListener("click",()=>safe(()=>loadLfsHistory()));
$("lfs-history-prev").addEventListener("click",()=>safe(()=>loadLfsHistory(Math.max(0,lfsHistory.offset-20))));
$("lfs-history-next").addEventListener("click",()=>safe(()=>loadLfsHistory(lfsHistory.offset+20)));
$("language").addEventListener("change",renderLfsCache);
let backupStorageDirty = false;
let backupScope=null,backupScopeRevision=0,backupRulesLoaded=false,backupScopeBusy=false;
function resetBackupScope() {
  ++backupScopeRevision;backupScope=null;backupRulesLoaded=false;backupScopeBusy=false;
  $("backup-rules").value="";renderBackupScope();
}
function backupRules() {return $("backup-rules").value.split(/\r?\n/).filter(line=>line.length>0);}
function renderBackupScope() {
  $("backup-rules").disabled=$("backup-rules-reload").disabled=$("backup-scope-preview").disabled=backupScopeBusy;
  $("backup-rules-save").disabled=backupScopeBusy || !backupScope?.rules_changed;
  $("backup-scope-run").disabled=backupScopeBusy || !backupScope || backupScope.rules_changed;
  $("backup-scope-prev").disabled=backupScopeBusy || !backupScope || backupScope.offset===0;
  $("backup-scope-next").disabled=backupScopeBusy || !backupScope?.has_more;
  $("backup-scope-summary").removeAttribute("data-i18n");$("backup-scope-summary").textContent="";
  $("backup-scope-entries").replaceChildren();
  if(!backupScope) return;
  translated($("backup-scope-summary"),"{count} included files · {bytes} bytes · {skipped} skipped entries",{count:backupScope.files,bytes:backupScope.bytes,skipped:backupScope.skipped});
  for(const row of backupScope.entries) {
    const reason=row.included?t("Included"):t(row.reason==="rule"?"Matched rule":row.reason==="protected"?"Recovery originals":row.reason==="link"?"Link":"Special file");
    const storage=row.storage==="blocks-v1"?t("Fixed blocks (4 MiB)"):row.storage==="blocks-cdc-v1"?t("Content-defined blocks (1–8 MiB)"):t("Whole files");
    $("backup-scope-entries").append(text("p",row.path+" / "+reason+(row.rule?": "+row.rule:"")+(row.included?" / "+row.bytes+" B / "+storage:"")));
  }
}
async function previewBackupScope(offset=0,continuation=false) {
  if(!selected || backupScopeBusy) return;
  const project=selected,revision=++backupScopeRevision,input={excludes:backupRules(),offset,limit:100};
  if(continuation && backupScope) input.inventory_hash=backupScope.inventory_hash;
  backupScopeBusy=true;renderBackupScope();
  try {
    const result=await api("projects/"+project+"/backup-preview","POST",input);
    if(project===selected && revision===backupScopeRevision) backupScope=result;
  } catch(error) {if(project===selected && revision===backupScopeRevision) backupScope=null;throw error;}
  finally {if(project===selected && revision===backupScopeRevision) {backupScopeBusy=false;renderBackupScope();}}
}
$("backup-rules").addEventListener("input",()=>{++backupScopeRevision;backupScope=null;renderBackupScope();});
$("backup-rules-reload").addEventListener("click",()=>{++backupScopeRevision;backupScope=null;$("backup-rules").value=(selectedProject?.excludes||[]).join("\n");renderBackupScope();});
$("backup-scope-preview").addEventListener("click",()=>safe(()=>previewBackupScope()));
$("backup-scope-prev").addEventListener("click",()=>safe(()=>previewBackupScope(Math.max(0,backupScope.offset-100),true)));
$("backup-scope-next").addEventListener("click",()=>safe(()=>previewBackupScope(backupScope.offset+100,true)));
async function applyBackupScope(saveRules) {
  if(!backupScope || backupScopeBusy || backupScope.rules_changed!==saveRules) return;
  const project=selected,revision=++backupScopeRevision,input={plan:backupScope.plan};
  if(saveRules) input.excludes=backupScope.excludes;
  backupScopeBusy=true;renderBackupScope();
  try {
    await api("projects/"+project+(saveRules?"/backup-excludes":"/backup"),saveRules?"PUT":"POST",input);
    if(project===selected && revision===backupScopeRevision) {backupScope=null;await refresh();}
  } catch(error) {if(project===selected && revision===backupScopeRevision) backupScope=null;throw error;}
  finally {if(project===selected && revision===backupScopeRevision) {backupScopeBusy=false;renderBackupScope();}}
}
$("backup-rules-save").addEventListener("click",()=>safe(()=>applyBackupScope(true)));
$("backup-scope-run").addEventListener("click",()=>safe(()=>applyBackupScope(false)));
$("language").addEventListener("change",renderBackupScope);
let loadedPlugins=null, availableModes=[], modeOptions=null, modeOptionsProject="", modeOptionsGeneration=0, modeOptionsDirty=false;
function pluginLabel(value,key) {return document.documentElement.lang==="fr" && value[key+"_fr"]?value[key+"_fr"]:value[key]||"";}
function renderPlugins() {
  if(!loadedPlugins) return;
  $("plugins-list").replaceChildren();
  if(!loadedPlugins.plugins.length) $("plugins-list").append(label("p","No plugins loaded"));
  for(const plugin of loadedPlugins.plugins) {
    const card=text("div","","snapshot");card.append(text("strong",pluginLabel(plugin,"name")+(plugin.version?" / "+plugin.version:"")),text("p",pluginLabel(plugin,"description")));
    for(const mode of plugin.modes) card.append(text("p",pluginLabel(mode,"label")+" — "+pluginLabel(mode,"description")));
    const details=document.createElement("details");details.append(label("summary","Plugin details"),text("p",plugin.id+" / ABI "+plugin.abi_version),text("p",plugin.path),text("p","SHA-256: "+plugin.sha256));card.append(details);
    $("plugins-list").append(card);
  }
}
$("plugins-refresh").addEventListener("click",()=>safe(async()=>{loadedPlugins=await api("plugins");renderPlugins();}));
function renderModeChoices() {
  const select=$("create").elements.mode,current=select.value;
  select.replaceChildren(...availableModes.map(mode=>{const option=document.createElement("option");option.value=mode.id;
    option.textContent=mode.plugin?pluginLabel(mode,"label"):t(mode.id==="sendreceive"?"Two-way synchronization":mode.label);return option;}));
  select.value=availableModes.some(mode=>mode.id===current)?current:(availableModes[0]?.id||"");
}
function resetModeOptions() {
  ++modeOptionsGeneration;modeOptions=null;modeOptionsProject="";modeOptionsDirty=false;
  $("mode-options-panel").hidden=true;$("mode-options-fields").replaceChildren();$("mode-options-note").removeAttribute("data-i18n");$("mode-options-note").textContent="";
}
function renderModeOptions() {
  if(!modeOptions || modeOptionsProject!==selected) return;
  const fields=$("mode-options-fields");fields.replaceChildren();
  $("mode-options-note").removeAttribute("data-i18n");
  $("mode-options-note").textContent=!modeOptions.available?t("Load the plugin providing this mode."):
    modeOptions.errors.length?modeOptions.errors.join("\n"):modeOptions.fields.length?"":t("No configurable options");
  $("mode-options-save").disabled=$("mode-options-defaults").disabled=!modeOptions.available || (!modeOptions.fields.length && modeOptions.valid);
  for(const field of modeOptions.fields) {
    const row=document.createElement("label"),input=document.createElement("input");
    row.append(text("span",pluginLabel(field,"label")));input.name=field.id;
    if(field.type==="boolean") {input.type="checkbox";input.checked=modeOptions.values[field.id];}
    else {input.type=field.type==="integer"?"number":"text";input.value=modeOptions.values[field.id];
      if(field.type==="integer") {input.min=field.minimum;input.max=field.maximum;input.step="1";input.required=true;}
      else input.maxLength=field.max_length;
    }
    input.addEventListener("input",()=>{modeOptions.values[field.id]=field.type==="boolean"?input.checked:field.type==="integer"?Number(input.value):input.value;modeOptionsDirty=true;++modeOptionsGeneration;$("mode-options-note").removeAttribute("data-i18n");$("mode-options-note").textContent="";});
    row.append(input);const description=pluginLabel(field,"description");if(description) row.append(text("small",description));
    if(field.type==="integer") row.append(label("small","Range: {min} to {max}",{min:field.minimum,max:field.maximum}));
    if(field.type==="string") row.append(label("small","Maximum {count} UTF-8 bytes",{count:field.max_length}));
    fields.append(row);
  }
}
async function loadModeOptions() {
  const project=selected,generation=++modeOptionsGeneration;if(!project) return;
  const result=await api("projects/"+project+"/mode-options");
  if(project!==selected || generation!==modeOptionsGeneration) return;
  modeOptions=result;modeOptionsProject=project;modeOptionsDirty=false;$("mode-options-panel").hidden=!result.plugin && result.available;renderModeOptions();
}
$("mode-options-reload").addEventListener("click",()=>safe(loadModeOptions));
$("mode-options-defaults").addEventListener("click",()=>{if(!modeOptions) return;modeOptions.values={...modeOptions.defaults};modeOptionsDirty=true;++modeOptionsGeneration;renderModeOptions();});
$("mode-options-form").addEventListener("submit",event=>{event.preventDefault();safe(async()=>{
  if(!modeOptions?.available || modeOptionsProject!==selected) return;
  const project=selected,generation=++modeOptionsGeneration,input={plan:modeOptions.plan,values:{...modeOptions.values}};
  $("mode-options-save").disabled=true;
  try {
    const result=await api("projects/"+project+"/mode-options","PUT",input);
    if(project!==selected || generation!==modeOptionsGeneration) return;
    modeOptions=result;modeOptionsDirty=false;renderModeOptions();translated($("mode-options-note"),"Mode options saved");
  } finally {if(project===selected && modeOptions) $("mode-options-save").disabled=!modeOptions.available || (!modeOptions.fields.length && modeOptions.valid);}
});});
$("language").addEventListener("change",()=>{renderPlugins();renderModeChoices();renderModeOptions();});
let backupFiles=null, backupRestore=null, backupComparison=null, backupGeneration=0, backupCompareGeneration=0, backupFileGeneration=0, backupRestoreGeneration=0;
function invalidateBackupRestore() {
  ++backupRestoreGeneration;
  backupRestore=null; $("backup-restore").disabled=true; $("backup-restore-summary").textContent="";
}
function resetBackupBrowser() {
  ++backupGeneration; ++backupCompareGeneration; backupFiles=backupComparison=null;
  invalidateBackupRestore();
  for(const id of ["backup-prefix","backup-paths","backup-destination"]) $(id).value="";
  for(const id of ["backup-files","backup-changes"]) $(id).replaceChildren();
  for(const id of ["backup-files-summary","backup-compare-summary","backup-patch"]) $(id).textContent="";
  for(const id of ["backup-prev","backup-next","backup-compare-prev","backup-compare-next"]) $(id).disabled=true;
}
function backupPaths() {return $("backup-paths").value.split(/\r?\n/).filter(line=>line.length>0);}
function backupRestoreInput() {
  const input={snapshot:$("backup-snapshot").value,destination:$("backup-destination").value},paths=backupPaths();
  if(paths.length) input.paths=paths;
  if(backupFiles && backupFiles.snapshot===input.snapshot) input.manifest_hash=backupFiles.manifest_hash;
  return input;
}
function populateBackupSelectors(snapshots) {
  for(const id of ["backup-snapshot","backup-left","backup-right"]) {
    const current=$(id).value, fallback=id==="backup-left"?snapshots.at(-1)?.id:snapshots[0]?.id;
    $(id).replaceChildren(...snapshots.map(s=>{const option=document.createElement("option");option.value=s.id;option.textContent=s.created+" / "+s.id;return option;}));
    $(id).value=snapshots.some(s=>s.id===current)?current:(fallback||"");
    if(current && current!==$(id).value) {
      if(id==="backup-snapshot") {++backupGeneration; backupFiles=null; invalidateBackupRestore(); $("backup-files").replaceChildren();}
      else {++backupCompareGeneration; backupComparison=null; $("backup-changes").replaceChildren(); $("backup-patch").textContent="";}
    }
  }
}
async function browseBackup(offset=0,continuation=false) {
  const project=selected,snapshot=$("backup-snapshot").value,prefix=$("backup-prefix").value;
  if(!project || !snapshot) return;
  const generation=++backupGeneration,input={snapshot,prefix,offset,limit:100};
  if(continuation && backupFiles) input.manifest_hash=backupFiles.manifest_hash;
  const result=await api("projects/"+project+"/snapshot-files","POST",input);
  if(project!==selected || generation!==backupGeneration || snapshot!==$("backup-snapshot").value || prefix!==$("backup-prefix").value) return;
  backupFiles=result; invalidateBackupRestore();
  $("backup-prev").disabled=result.offset===0; $("backup-next").disabled=!result.has_more;
  translated($("backup-files-summary"),"{count} files · {bytes} bytes",{count:result.total,bytes:result.bytes});
  $("backup-files").replaceChildren(...result.files.map(entry=>{
    const button=text("button",entry.path+" / "+entry.size+" B");
    button.addEventListener("click",()=>{
      const paths=new Set(backupPaths()); paths.add(entry.path); $("backup-paths").value=[...paths].join("\n"); invalidateBackupRestore();
    });return button;
  }));
}
$("snapshot-browser-form").addEventListener("submit",event=>{event.preventDefault();safe(()=>browseBackup());});
$("backup-prefix").addEventListener("input",()=>{++backupGeneration;backupFiles=null;$("backup-files").replaceChildren();$("backup-prev").disabled=$("backup-next").disabled=true;invalidateBackupRestore();});
$("backup-snapshot").addEventListener("change",()=>{++backupGeneration;backupFiles=null;$("backup-files").replaceChildren();$("backup-paths").value="";$("backup-prev").disabled=$("backup-next").disabled=true;invalidateBackupRestore();});
$("backup-add-folder").addEventListener("click",()=>{
  const prefix=$("backup-prefix").value;if(!prefix) return;
  const paths=new Set(backupPaths()); paths.add(prefix.endsWith("/")?prefix:prefix+"/");$("backup-paths").value=[...paths].join("\n");invalidateBackupRestore();
});
$("backup-prev").addEventListener("click",()=>safe(()=>browseBackup(Math.max(0,(backupFiles?.offset||0)-100),true)));
$("backup-next").addEventListener("click",()=>safe(()=>browseBackup((backupFiles?.offset||0)+100,true)));
$("backup-restore-form").addEventListener("input",invalidateBackupRestore);
$("backup-restore-form").addEventListener("submit",event=>{event.preventDefault();safe(async()=>{
  const project=selected,input=backupRestoreInput(),generation=backupGeneration;
  if(!project || !input.snapshot) return; invalidateBackupRestore();
  const restoreGeneration=backupRestoreGeneration;
  const result=await api("projects/"+project+"/restore-plan","POST",input);
  if(project!==selected || generation!==backupGeneration || restoreGeneration!==backupRestoreGeneration || JSON.stringify(input)!==JSON.stringify(backupRestoreInput())) return;
  backupRestore=result.ok?{project,input,plan:result.plan}:null;$("backup-restore").disabled=!backupRestore;
  $("backup-restore-summary").textContent=[t(result.ok?"Integrity verified":"Integrity check failed"),
    t("{count} files · {bytes} bytes",{count:result.files,bytes:result.bytes}),t("Destination")+": "+result.destination,
    ...result.issues.map(issue=>issue.path+": "+issue.error),...result.entries.slice(0,100).map(entry=>entry.path+" / "+entry.size+" B")].join("\n");
});});
$("backup-restore").addEventListener("click",()=>safe(async()=>{
  const preview=backupRestore;if(!preview || preview.project!==selected || JSON.stringify(preview.input)!==JSON.stringify(backupRestoreInput())) return;
  invalidateBackupRestore();
  const restoreGeneration=backupRestoreGeneration;
  const result=await api("projects/"+preview.project+"/restore","POST",{...preview.input,plan:preview.plan});
  if(preview.project===selected && restoreGeneration===backupRestoreGeneration) $("backup-restore-summary").textContent=t("Restoration complete")+" / "+result.files+" / "+result.destination;
}));
function backupComparisonInput() {return {left:$("backup-left").value,right:$("backup-right").value};}
async function compareBackups(offset=0,continuation=false) {
  const project=selected,input={...backupComparisonInput(),offset,limit:100},generation=++backupCompareGeneration;
  if(!project || !input.left || !input.right) return;
  if(continuation && backupComparison) {input.left_hash=backupComparison.left_hash;input.right_hash=backupComparison.right_hash;}
  const result=await api("projects/"+project+"/snapshot-compare","POST",input);
  if(project!==selected || generation!==backupCompareGeneration) return;
  backupComparison=result;$("backup-patch").textContent="";
  $("backup-compare-prev").disabled=result.offset===0;$("backup-compare-next").disabled=!result.has_more;
  translated($("backup-compare-summary"),"Changed files: {count}",{count:result.total});
  $("backup-changes").replaceChildren(...result.files.map(entry=>{
    const names={added:"Added",deleted:"Deleted",modified:"Modified",permissions:"Permissions changed"};
    const button=text("button",t(names[entry.status])+" / "+entry.path);
    button.addEventListener("click",()=>safe(async()=>{
      const fileGeneration=++backupFileGeneration;
      const query={left:result.left,right:result.right,left_hash:result.left_hash,right_hash:result.right_hash,path:entry.path};
      const diff=await api("projects/"+project+"/snapshot-compare-file","POST",query);
      if(project===selected && generation===backupCompareGeneration && fileGeneration===backupFileGeneration) {
        $("backup-patch").textContent=(diff.permissions_changed?t("Permissions changed")+"\n":"")+(diff.binary?t(diff.message):diff.patch||t("No differences"));
      }
    })); return button;
  }));
}
for(const id of ["backup-left","backup-right"]) $(id).addEventListener("change",()=>{++backupCompareGeneration;backupComparison=null;$("backup-changes").replaceChildren();$("backup-patch").textContent="";$("backup-compare-prev").disabled=$("backup-compare-next").disabled=true;});
$("snapshot-compare-form").addEventListener("submit",event=>{event.preventDefault();safe(()=>compareBackups());});
$("backup-compare-prev").addEventListener("click",()=>safe(()=>compareBackups(Math.max(0,(backupComparison?.offset||0)-100),true)));
$("backup-compare-next").addEventListener("click",()=>safe(()=>compareBackups((backupComparison?.offset||0)+100,true)));
let gitSettingsDirty = false;
let gitSetupPlan = null, gitSetupLoad = 0;
function gitSetupInput() { return {branch:$("git-setup-branch").value,object_format:$("git-setup-format").value}; }
function invalidateGitSetup() { gitSetupPlan=null; ++gitSetupLoad; $("git-setup-apply").disabled=true; }
function showGitSetup(project,input,result) {
  $("git-setup-root").textContent=result.root;
  $("git-setup-panel").hidden=!result.can_initialize; $("git-ready").hidden=result.can_initialize;
  gitSetupPlan=result.can_initialize?{project,input,plan:result.plan}:null;
  $("git-setup-apply").disabled=!gitSetupPlan;
}
$("git-setup-form").addEventListener("input",invalidateGitSetup);
$("git-setup-form").addEventListener("submit",event=>{
  event.preventDefault(); safe(async()=>{
    const project=selected,input=gitSetupInput(); if(!project) return;
    invalidateGitSetup(); const request=gitSetupLoad;
    const result=await api("projects/"+project+"/git-setup","POST",input);
    if(project!==selected || request!==gitSetupLoad || JSON.stringify(input)!==JSON.stringify(gitSetupInput())) return;
    showGitSetup(project,input,result);
    if(!result.can_initialize) await loadGitExchange();
  });
});
$("git-setup-apply").addEventListener("click",()=>safe(async()=>{
  const preview=gitSetupPlan; if(!preview || preview.project!==selected) return;
  invalidateGitSetup();
  await api("projects/"+preview.project+"/git-setup","POST",{...preview.input,apply:true,plan:preview.plan});
  if(selected===preview.project) await loadGitExchange();
}));
let gitStorageSelected = new Set(), gitStoragePlan = null, gitStorageLoad = 0;
function invalidateGitStorage() {
  gitStoragePlan = null; ++gitStorageLoad;
  $("git-storage-apply").disabled = true;
}
function resetGitStorage() {
  invalidateGitStorage(); gitStorageSelected.clear();
  $("git-storage-exports").replaceChildren(); $("git-storage-summary").replaceChildren();
}
$("git-storage-preview").addEventListener("click", () => safe(async () => {
  const project = selected;
  if (!project) return;
  invalidateGitStorage();
  const request = gitStorageLoad, exports = [...gitStorageSelected].sort();
  const result = await api("projects/" + project + "/git-storage", "POST", {exports});
  if (selected !== project || request !== gitStorageLoad) return;
  gitStoragePlan = {project, exports, plan: result.plan};
  $("git-storage-apply").disabled = !result.can_prune;
  $("git-storage-exports").replaceChildren();
  for (const entry of result.exports) {
    const row = document.createElement("label"), checkbox = document.createElement("input");
    checkbox.type = "checkbox"; checkbox.checked = gitStorageSelected.has(entry.id);
    checkbox.addEventListener("change", () => {
      if (checkbox.checked) gitStorageSelected.add(entry.id); else gitStorageSelected.delete(entry.id);
      invalidateGitStorage();
    });
    row.append(checkbox, text("span", entry.branch + " / " + entry.id));
    $("git-storage-exports").append(row);
  }
  $("git-storage-summary").replaceChildren(label("p", "{count} unused LFS objects · {bytes} reclaimable bytes", {count: result.unused_objects, bytes: result.reclaimable_bytes}));
  for (const issue of result.errors) $("git-storage-summary").append(text("p", issue.error));
}));
$("git-storage-apply").addEventListener("click", () => safe(async () => {
  const input = gitStoragePlan;
  if (!input || input.project !== selected) return;
  invalidateGitStorage();
  const result = await api("projects/" + input.project + "/git-storage", "POST", {exports: input.exports, plan: input.plan, apply: true});
  if (selected !== input.project) return;
  resetGitStorage();
  $("git-storage-summary").append(text("pre", JSON.stringify(result, null, 2)));
  await loadGitExchange();
}));
$("git-settings").addEventListener("input", () => {gitSettingsDirty=true; $("git-settings-result").replaceChildren();});
function gitSettingsInput() { return {git_lfs: $("git-policy").checked, git_lfs_storage: $("git-storage-path").value}; }
$("git-settings").addEventListener("submit", event => {
  event.preventDefault();
  const project=selected, input=gitSettingsInput(), button=event.currentTarget.querySelector("button");
  if (!project) return;
  safe(async () => {
    button.disabled=true;
    try {
      const result=await api("projects/"+project,"PUT",input);
      if (selected!==project || JSON.stringify(input)!==JSON.stringify(gitSettingsInput())) return;
      selectedProject=result; gitSettingsDirty=false;
      $("git-settings-result").replaceChildren(label("span","Git settings saved"));
    } finally {button.disabled=false;}
  });
});
let transferLimitsDirty = false, transferLimitsRevision = 0;
function transferLimitsInput() {
  return {file_limit_mib: Number($("transfer-file-mib").value),
    inbox_quota_mib: Number($("transfer-inbox-mib").value), outbox_quota_mib: Number($("transfer-outbox-mib").value)};
}
function showTransferLimits(result) {
  if (!transferLimitsDirty) {
    $("transfer-file-mib").value = result.file_limit_mib;
    $("transfer-inbox-mib").value = result.inbox_quota_mib;
    $("transfer-outbox-mib").value = result.outbox_quota_mib;
  }
  $("transfer-limits-usage").replaceChildren(
    label("p", "Inbox: {bytes} B · reserved incoming: {incoming} B · reserved outgoing: {outgoing} B",
      {bytes: result.inbox_bytes, incoming: result.reserved_incoming_bytes, outgoing: result.reserved_outgoing_bytes}));
}
$("transfer-limits-form").addEventListener("input", () => { transferLimitsDirty = true; });
$("transfer-limits-form").addEventListener("submit", event => {
  event.preventDefault();
  const input = transferLimitsInput();
  const button = event.currentTarget.querySelector("button");
  safe(async () => {
    button.disabled = true;
    try {
      const result = await api("network/transfer-configure", "POST", input);
      ++transferLimitsRevision;
      if (JSON.stringify(input) === JSON.stringify(transferLimitsInput())) transferLimitsDirty = false;
      showTransferLimits(result);
    } finally { button.disabled = false; }
  });
});
function serviceInput() {
  return {platform: $("service-platform").value, executable: $("service-executable").value,
    target_state: $("service-state").value, port: Number($("service-port").value)};
}
function serviceExportState() {
  $("service-export").disabled = !servicePreview || !$("service-directory").value;
}
function clearServicePreview() {
  servicePreview = null;
  $("service-preview").textContent = "";
  $("service-preview").hidden = true;
  $("service-result").replaceChildren();
  serviceExportState();
}
$("service-form").addEventListener("input", clearServicePreview);
$("service-form").addEventListener("change", clearServicePreview);
$("service-directory").addEventListener("input", () => {
  $("service-result").replaceChildren();
  serviceExportState();
});
$("service-form").addEventListener("submit", event => {
  event.preventDefault();
  safe(async () => {
    clearServicePreview();
    const data = serviceInput();
    const result = await api("service/plan", "POST", data);
    if (JSON.stringify(data) !== JSON.stringify(serviceInput())) return;
    servicePreview = {data, plan: result.plan};
    $("service-preview").textContent = result.filename + "\n\n" + result.contents;
    $("service-preview").hidden = false;
    serviceExportState();
  });
});
$("service-export").addEventListener("click", () => safe(async () => {
  const preview = servicePreview, destination = $("service-directory").value;
  if (!preview || JSON.stringify(preview.data) !== JSON.stringify(serviceInput())) return;
  const result = await api("service/export", "POST", {...preview.data, plan: preview.plan, destination});
  if (preview !== servicePreview || destination !== $("service-directory").value) return;
  $("service-result").replaceChildren(label("p", "Service files exported"), text("div", result.directory));
}));
let currentPatch = "";
let selectedProject = null, resolutionPath = "", resolutionPreview = null, resolutionExpected = null;
function resolutionInput() {
  const data = {path: resolutionPath, choice: $("resolution-choice").value};
  if (resolutionExpected) {
    data.expected_current = resolutionExpected.current;
    data.expected_conflict = resolutionExpected.conflict;
  }
  if (data.choice === "merged") data.content = $("resolution-content").value;
  return data;
}
function clearResolutionPreview() {
  resolutionPreview = null;
  $("resolution-apply").disabled = true;
  $("resolution-summary").replaceChildren();
}
function openResolution(diff, path) {
  resolutionPath = path;
  resolutionExpected = {current: diff.before_info, conflict: diff.after_info};
  clearResolutionPreview();
  $("resolution-panel").hidden = false;
  $("resolution-path").textContent = diff.left + " ← " + diff.right;
  $("resolution-choice").value = "current";
  translated($("resolution-choice").options[0], diff.before_info.exists ? "Keep current version" : "Keep deletion");
  $("resolution-choice").options[2].disabled = diff.binary;
  $("resolution-content").value = diff.binary ? "" : (diff.before_info.exists ? diff.before : diff.after).replace(/\r\n/g, "\n");
  $("resolution-content").hidden = true;
  $("resolution-editor-note").hidden = true;
}
function showDiff(diff) {
  currentPatch = diff.patch || "";
  $("patch").removeAttribute("data-i18n");
  if (diff.binary || currentPatch) $("patch").textContent = diff.binary ? diff.message : currentPatch;
  else translated($("patch"), "No differences");
  $("copy-patch").disabled = currentPatch.length === 0;
}
$("token").value = token;
async function api(path, method = "GET", data) {
  const response = await fetch("/api/" + path, {method, headers: {"Authorization": "Bearer " + token, "Content-Type": "application/json"}, body: data === undefined ? undefined : JSON.stringify(data)});
  const result = await response.json();
  if (!response.ok) throw new Error(result.error || t("Operation refused"));
  return result;
}
function text(tag, content, className) { const node = document.createElement(tag); node.textContent = content; if (className) node.className = className; return node; }
function label(tag, key, values = {}, className) { return translated(text(tag, "", className), key, values); }
async function safe(fn) { $("error").textContent = ""; try { await fn(); } catch (error) { $("error").textContent = error.message; } }
async function diagnostics(probe) {
  const result = await api("doctor", probe ? "POST" : "GET", probe ? {probe_tools: true, probe_engine: true} : undefined);
  $("doctor-output").replaceChildren(label("p", "{errors} errors · {warnings} warnings", result));
  for (const row of result.checks) {
    const card = text("div", "", "snapshot");
    card.append(row.details.project ? text("strong", row.label) : label("strong", row.label),
      text("span", " · "), label("span", row.message));
    if (row.hint) card.append(label("p", row.hint));
    if (row.details.path) card.append(text("div", row.details.path));
    if (row.details.version) card.append(text("div", row.details.version));
    $("doctor-output").append(card);
  }
}
$("doctor-inspect").addEventListener("click", () => safe(() => diagnostics(false)));
$("doctor-probe").addEventListener("click", () => safe(() => diagnostics(true)));
async function refresh() {
  const data = await api("overview");
  availableModes=data.modes;renderModeChoices();
  $("summary").replaceChildren(text("div", String(data.projects.length), "stat"), label("div", "{projects} projects · {backups} with a backup", {projects: data.projects.length, backups: data.projects.filter(p => p.latest_snapshot).length}));
  $("syncthing").href = data.syncthing_url;
  $("projects").replaceChildren();
  for (const project of data.projects) {
    const button = text("button", project.name);
    button.addEventListener("click", () => safe(() => select(project)));
    $("projects").append(button);
  }
  const events = await api("events");
  $("events").replaceChildren(...events.slice(0, 30).map(e => text("div", e.created.slice(0, 19) + " · " + e.kind + " · " + e.message, "event")));
  if (selected) {
    const project = data.projects.find(p => p.id === selected);
    if (project) await select(project);
  }
}
async function select(project) {
  if (selected !== project.id) {
    resetLfsCache();
    resetBackupScope();
    resetModeOptions();
    resetBackupBrowser();
    backupStorageDirty = false;
    gitSettingsDirty = false;
    $("git-settings-result").replaceChildren();
    $("backup-storage-result").replaceChildren();
    $("changes").replaceChildren();
    $("conflicts").replaceChildren();
    showDiff({patch: ""});
    resolutionPath = "";
    resolutionExpected = null;
    clearResolutionPreview();
    $("resolution-panel").hidden = true;
    $("recovery-records").replaceChildren();
    $("received-records").replaceChildren();
    $("transfer-records").replaceChildren();
    $("git-records").replaceChildren();
    $("git-integrations-records").replaceChildren(); ++gitReceiptsLoad;
    resetMaterialization();
    $("git-result").replaceChildren();
    $("git-branches").replaceChildren(); clearGitReview();
    resetGitStorage();
    invalidateGitSetup(); $("git-setup-branch").value="main"; $("git-setup-format").value="sha1";
    $("git-setup-panel").hidden=true; $("git-ready").hidden=true;
    ++gitLoad;
    $("transfer-result").replaceChildren();
    $("transfer-peer").value = "";
    $("transfer-path").value = "";
    $("transfer-legacy").checked = false;
    ++transferLoad;
  }
  selected = project.id;
  selectedProject = project;
  if(!backupRulesLoaded) {$("backup-rules").value=(project.excludes||[]).join("\n");backupRulesLoaded=true;}
  $("git-panel").hidden = project.mode !== "git-bundles";
  if (!gitSettingsDirty) {
    $("git-policy").checked=project.git_lfs!==false;
    $("git-storage-path").value=project.git_lfs_storage || "";
  }
  $("recovery-panel").hidden = false;
  $("received-panel").hidden = false;
  $("transfer-panel").hidden = false;
  $("resolution-pause").disabled = project.paused;
  $("resolution-apply").disabled = !resolutionPreview || !resolutionPreview.spaceAvailable || !project.paused || !!project.resolution_pending;
  $("detail").hidden = false;
  $("project-name").textContent = project.name;
  $("project-root").textContent = project.root + " · " + project.mode;
  if (!backupStorageDirty) $("backup-storage-policy").value = project.backup_storage || "auto";
  const snapshots = await api("projects/" + selected + "/snapshots");
  if (selected !== project.id) return;
  populateBackupSelectors(snapshots);
  $("snapshots").replaceChildren(...snapshots.map(s => {
    const card = text("div", "", "snapshot");
    card.append(label("div", "{created} · {count} files · {id}", {created: s.created.slice(0, 19), count: s.file_count, id: s.id}));
    if (s.block_storage) card.append(label("p", "Block data: {written} bytes written · {reused} bytes reused", {written: s.block_storage.new_block_bytes, reused: s.block_storage.reused_block_bytes}));
    if (s.block_storage?.content_defined_files) card.append(label("p", "Content-defined file versions: {count}", {count:s.block_storage.content_defined_files}));
    return card;
  }));
  const revision = transferLimitsRevision;
  const limits = await api("network/transfer-limits");
  if (selected === project.id && revision === transferLimitsRevision) showTransferLimits(limits);
  if (selected===project.id && !modeOptionsDirty) await loadModeOptions();
}
$("backup-storage-policy").addEventListener("change", () => {
  backupStorageDirty = true;
  $("backup-storage-result").replaceChildren();
});
$("backup-storage-form").addEventListener("submit", event => {
  event.preventDefault();
  const project = selected, policy = $("backup-storage-policy").value;
  if (!project) return;
  const button = event.currentTarget.querySelector("button");
  safe(async () => {
    button.disabled = true;
    try {
      const updated = await api("projects/" + project, "PUT", {backup_storage: policy});
      if (selected !== project || $("backup-storage-policy").value !== policy) return;
      selectedProject = updated;
      backupStorageDirty = false;
      $("backup-storage-result").replaceChildren(label("span", "Backup storage policy saved"));
    } finally { button.disabled = false; }
  });
});
async function action(name) {
  if (!selected) return;
  const project = selected;
  const result = await api("projects/" + project + "/" + name, "POST", {});
  if (project !== selected) return;
  $("output").textContent = JSON.stringify(result, null, 2);
  if (name === "changes") {
    $("changes").replaceChildren();
    for (const file of result.files) {
      const button = text("button", file.status + " · " + file.path, file.status);
      button.addEventListener("click", () => safe(async () => {
        const diff = await api("projects/" + project + "/diff", "POST", {path: file.path, snapshot: result.baseline});
        if (project === selected) showDiff(diff);
      }));
      $("changes").append(button);
    }
  }
  if (name === "conflicts") {
    $("conflicts").replaceChildren();
    if (result.files.length === 0) $("conflicts").append(label("p", "No conflict copies found."));
    for (const file of result.files) {
      const button = text("button", file.original + " · " + file.timestamp + " · " + file.modified_by);
      button.addEventListener("click", () => safe(async () => {
        const diff = await api("projects/" + project + "/conflict-diff", "POST", {path: file.path});
        if (project === selected) { showDiff(diff); openResolution(diff, file.path); }
      }));
      $("conflicts").append(button);
    }
    for (const error of result.errors) $("conflicts").append(text("p", error.path + ": " + error.error));
  }
  await refresh();
}
$("connect").addEventListener("click", () => safe(async () => {
  token = $("token").value.trim();
  try { sessionStorage.setItem("cysynck-token", token); } catch (_) { /* Keep the key in memory. */ }
  await refresh();
}));
$("refresh").addEventListener("click", () => safe(refresh));
$("resolution-choice").addEventListener("change", () => {
  clearResolutionPreview();
  const manual = $("resolution-choice").value === "merged";
  $("resolution-content").hidden = !manual;
  $("resolution-editor-note").hidden = !manual;
});
$("resolution-content").addEventListener("input", clearResolutionPreview);
$("resolution-pause").addEventListener("click", () => safe(async () => {
  const project = selected;
  if (!project) return;
  await api("projects/" + project + "/pause", "POST", {paused: true});
  await refresh();
}));
$("resolution-prepare").addEventListener("click", () => safe(async () => {
  const project = selected, data = resolutionInput();
  clearResolutionPreview();
  if (!project || !data.path) return;
  if (data.choice === "merged" && new TextEncoder().encode(data.content).length > 512 * 1024) throw new Error(t("The merge exceeds 512 KiB."));
  const result = await api("projects/" + project + "/resolution-plan", "POST", data);
  if (project !== selected || JSON.stringify(data) !== JSON.stringify(resolutionInput())) return;
  resolutionPreview = {project, data, plan: result.plan, spaceAvailable: result.space_available};
  $("resolution-summary").replaceChildren(text("div", result.original),
    label("div", "Result: {bytes} bytes", {bytes: result.result.bytes}),
    result.result.sha256 ? label("div", "SHA-256: {hash}", {hash: result.result.sha256}) : label("div", "Deletion kept"),
    label("div", "Recovery copies: approximately {bytes} bytes", {bytes: result.state_required_bytes}));
  if (!result.space_available) $("resolution-summary").append(label("div", "Not enough free space."));
  $("resolution-apply").disabled = !result.space_available || !selectedProject.paused || !!selectedProject.resolution_pending;
}));
$("resolution-apply").addEventListener("click", () => safe(async () => {
  const preview = resolutionPreview;
  if (!preview || preview.project !== selected || JSON.stringify(preview.data) !== JSON.stringify(resolutionInput())) return;
  clearResolutionPreview();
  const result = await api("projects/" + preview.project + "/resolve-conflict", "POST", {...preview.data, plan: preview.plan});
  if (preview.project !== selected) return;
  $("resolution-panel").hidden = true;
  $("output").replaceChildren(label("div", "Resolution applied. Synchronization is paused."), text("div", JSON.stringify(result, null, 2)));
  await refresh();
  await loadRecovery();
}));
async function loadRecovery() {
  const project = selected;
  if (!project) return;
  const result = await api("projects/" + project + "/resolutions", "POST", {});
  if (project !== selected) return;
  $("recovery-records").replaceChildren();
  for (const record of result.records) {
    const card = text("div", "", "snapshot");
    const heading = text("p", record.created + " · " + record.original + " · ");
    const phases = {preparing: "Preparing", prepared: "Prepared", "moving-current": "Moving current file", "moving-conflict": "Moving conflict copy", publishing: "Publishing result", applied: "Applied", interrupted: "Interrupted", "rolled-back": "Rolled back", aborted: "Aborted", acknowledged: "Reviewed"};
    heading.append(label("span", record.needs_attention ? "Needs recovery" : phases[record.phase] || record.phase));
    card.append(heading);
    const destination = document.createElement("input");
    destination.setAttribute("data-i18n-placeholder", "New separate folder on the server");
    destination.setAttribute("data-i18n-aria", "Recovery copies destination");
    const exportButton = label("button", "Export copies");
    exportButton.addEventListener("click", () => safe(async () => {
      const output = await api("projects/" + project + "/resolution-export", "POST", {resolution: record.id, destination: destination.value});
      if (project === selected) $("output").textContent = JSON.stringify(output, null, 2);
    }));
    card.append(destination, exportButton);
    if (selectedProject.resolution_pending === record.id) {
      card.append(label("p", "After restoring or checking the working files, mark this recovery as reviewed. The copies are kept."));
      const reviewed = label("button", "Mark as reviewed");
      reviewed.disabled = !selectedProject.paused;
      reviewed.addEventListener("click", () => safe(async () => {
        await api("projects/" + project + "/resolution-acknowledge", "POST", {resolution: record.id});
        await refresh();
        await loadRecovery();
      }));
      card.append(reviewed);
    }
    $("recovery-records").append(card);
    CySynckI18n.apply(card);
  }
  for (const error of result.errors) $("recovery-records").append(text("p", error.path + ": " + error.error));
}
$("recovery-load").addEventListener("click", () => safe(loadRecovery));
async function loadReceived() {
  const project = selected;
  if (!project) return;
  const result = await api("projects/" + project + "/received-files", "POST", {});
  if (selected !== project) return;
  $("received-records").replaceChildren();
  if (!result.files.length) $("received-records").append(label("p", "No received files"));
  for (const file of result.files) {
    const card = text("div", "", "snapshot");
    card.append(text("p", file.name + " · " + file.bytes + " B · " + file.peer), text("p", file.created + " · " + file.id), text("p", "SHA-256: " + file.sha256));
    if (!file.available) card.append(label("p", "Payload unavailable"));
    const verify = label("button", "Verify received file");
    const destination = document.createElement("input");
    destination.setAttribute("data-i18n-placeholder", "New separate folder on the server");
    destination.setAttribute("data-i18n-aria", "New separate folder on the server");
    const exportButton = label("button", "Export received file");
    const status = text("p", "");
    verify.disabled = !file.available;
    exportButton.disabled = true;
    destination.addEventListener("input", () => { exportButton.disabled = !file.available || !destination.value.trim(); });
    verify.addEventListener("click", () => safe(async () => {
      status.replaceChildren();
      await api("projects/" + project + "/received-verify", "POST", {transfer: file.id});
      if (project === selected) status.append(label("span", "Received file verified"));
    }));
    exportButton.addEventListener("click", () => safe(async () => {
      status.replaceChildren();
      const exported = await api("projects/" + project + "/received-export", "POST", {transfer: file.id, destination: destination.value});
      if (project === selected) status.append(label("span", "Received file exported"), text("span", " · " + exported.path));
    }));
    card.append(verify, destination, exportButton, status);
    $("received-records").append(card);
    CySynckI18n.apply(card);
  }
  for (const issue of result.errors) $("received-records").append(text("p", issue.id + ": " + issue.error));
  if (result.unindexed.length) {
    $("received-records").append(label("p", "Unindexed inbox files are preserved and require manual review."));
    for (const name of result.unindexed) $("received-records").append(text("p", name));
  }
}
$("received-load").addEventListener("click", () => safe(loadReceived));
let materializationPlan=null,materializationGeneration=0,materializationReceiptsGeneration=0;
function resetMaterialization(){materializationPlan=null;++materializationGeneration;++materializationReceiptsGeneration;$("git-materialization-receipts").replaceChildren();$("git-materialize-result").replaceChildren();$("git-materialize-apply").disabled=true;$("git-materialize-preview").disabled=false;}
async function loadMaterializationReceipts(offset=0){
  const project=selected,generation=++materializationReceiptsGeneration;if(!project)return;
  const result=await api("projects/"+project+"/git-materializations","POST",{offset});
  if(selected!==project || generation!==materializationReceiptsGeneration)return;
  const container=$("git-materialization-receipts");container.replaceChildren();
  if(!result.records.length)container.append(label("p","No LFS receipts"));
  for(const record of result.records){
    const card=text("div","","snapshot"),output=text("div",""),check=label("button","Check LFS receipt");
    card.append(text("p",record.created+" · "+record.id),label("p",record.status==="complete"?"LFS materialization complete":"LFS materialization incomplete"),text("p",record.recovery_directory));
    if(record.acknowledged)card.append(label("p","Recovery reviewed"));
    let revision=0;const current=()=>selected===project && generation===materializationReceiptsGeneration && card.isConnected;
    check.addEventListener("click",()=>safe(async()=>{
      output.replaceChildren();const request=++revision;
      const checked=await api("projects/"+project+"/git-materialization-check","POST",{receipt:record.id});
      if(!current() || request!==revision)return;
      const valid=checked.context_matches && !checked.errors.length;
      if(checked.context_matches && checked.index_metadata_matches===false)output.append(label("p","Index metadata refreshed; staged entries and flags match"));
      output.append(label("p",valid && checked.position==="after"?"LFS assets and recovery pointers verified":valid && checked.position==="before"?"LFS files match the original pointers":"LFS recovery needs manual inspection"));
      for(const file of checked.files)output.append(text("p",file.path+" / "+t(file.position==="before"?"Original pointer":file.position==="after"?"Verified asset":file.position==="interrupted"?"Publication interrupted":"Local contents differ")));
      for(const error of checked.errors)output.append(text("p",error));
      if(checked.can_acknowledge){const acknowledge=label("button","Mark as reviewed");output.append(acknowledge);acknowledge.addEventListener("click",()=>safe(async()=>{
        if(!current() || request!==revision)return;acknowledge.disabled=true;check.disabled=true;++revision;
        try{await api("projects/"+project+"/git-materialization-acknowledge","POST",{receipt:record.id,plan:checked.plan});if(current())await loadMaterializationReceipts(offset);}
        finally{check.disabled=false;}
      }));}
    }));
    card.append(check,output);container.append(card);
  }
  for(const issue of result.errors)container.append(text("p",issue.id+": "+issue.error));
  for(const name of result.unindexed)container.append(text("p",name));
  for(const [show,title,page] of [[offset>0,"Previous receipts",Math.max(0,offset-20)],[result.has_more,"Next receipts",offset+20]])if(show){const button=label("button",title);button.addEventListener("click",()=>safe(()=>loadMaterializationReceipts(page)));container.append(button);}
}
$("git-materialization-receipts-load").addEventListener("click",()=>safe(()=>loadMaterializationReceipts()));
function showMaterialization(result){
  const output=$("git-materialize-result");output.replaceChildren();
  for(const [key,title] of [["pending","Pending assets"],["materialized","Already materialized"],["skipped","Preserved files"],["bytes","Asset bytes"]])output.append(text("p",t(title)+": "+result[key]));
  if(result.applied)output.append(label("p","LFS materialization complete"),text("p",result.id),label("p","Recovery directory"),text("p",result.recovery_directory));
  const states={pointer:"Committed pointer",materialized:"Already materialized",modified:"Local modifications preserved",missing:"Local deletion preserved",staged:"Staged changes preserved",index_flags:"Special index flags preserved",read_only:"Read-only file preserved",attributes:"LFS attributes required",blocked:"Materialization blocked"};
  const files=document.createElement("details");files.append(label("summary","LFS file states"));
  for(const file of result.files)files.append(text("p",file.path+" · "+t(states[file.state]||"Materialization blocked")+(file.cache==="unavailable"?" · "+t("LFS cache object unavailable"):"")));
  output.append(files);
  for(const issue of result.errors)output.append(text("p",issue.path+": "+issue.error));
}
$("git-materialize-preview").addEventListener("click",()=>safe(async()=>{
  const project=selected,generation=++materializationGeneration;if(!project)return;
  materializationPlan=null;$("git-materialize-apply").disabled=true;$("git-materialize-preview").disabled=true;
  try{
    const result=await api("projects/"+project+"/git-materialize","POST",{});
    if(selected!==project || generation!==materializationGeneration)return;
    showMaterialization(result);
    if(result.can_materialize){materializationPlan={project,plan:result.plan,base:result.base,local_ref:result.local_ref};$("git-materialize-apply").disabled=false;}
  }finally{if(selected===project && generation===materializationGeneration)$("git-materialize-preview").disabled=false;}
}));
$("git-materialize-apply").addEventListener("click",()=>safe(async()=>{
  const saved=materializationPlan;if(!saved || selected!==saved.project)return;
  materializationPlan=null;const generation=++materializationGeneration;
  $("git-materialize-apply").disabled=true;$("git-materialize-preview").disabled=true;
  try{
    const result=await api("projects/"+saved.project+"/git-materialize","POST",{apply:true,plan:saved.plan,base:saved.base,local_ref:saved.local_ref});
    if(selected===saved.project && generation===materializationGeneration)showMaterialization(result);
  }finally{if(selected===saved.project && generation===materializationGeneration)$("git-materialize-preview").disabled=false;}
}));
async function loadGitReceipts(offset=0) {
  const project=selected,generation=++gitReceiptsLoad;
  if(!project)return;
  const result=await api("projects/"+project+"/git-integrations","POST",{offset});
  if(project!==selected || generation!==gitReceiptsLoad)return;
  const container=$("git-integrations-records");container.replaceChildren();
  if(!result.records.length)container.append(label("p","No integration receipts"));
  for(const record of result.records) {
    const card=text("div","","snapshot"),check=label("button","Check integration receipt"),output=text("div","");
    card.append(text("p",record.created+" / "+record.peer+" / "+record.branch),text("p",record.id),label("p",record.status==="complete"?"Integration complete":"Integration incomplete"),text("p",record.receipt_path));
    if(record.result_commit)card.append(label("p","Merge commit"),text("p",record.result_commit));
    if(record.resolved_conflicts!==undefined)card.append(label("p","Resolved conflicts"),text("p",String(record.resolved_conflicts)));
    if(record.edited_files!==undefined)card.append(label("p","Edited files"),text("p",String(record.edited_files)));
    if(record.acknowledged_at)card.append(label("p","Recovery reviewed"),text("p",record.acknowledged_at));
    let request=0;
    const current=()=>selected===project && generation===gitReceiptsLoad && card.isConnected;
    check.addEventListener("click",()=>safe(async()=>{
      output.replaceChildren();const revision=++request;
      const checked=await api("projects/"+project+"/git-integration-check","POST",{receipt:record.id});
      if(!current() || revision!==request)return;
      output.append(label("p",checked.position==="after"?"Repository at integrated commit":checked.position==="before"?"Repository at original commit":"Repository state differs or is unavailable"));
      if(checked.tracked_matches)output.append(label("p","Tracked files and index verified"));
      if(checked.local_data_matches===null)output.append(label("p","Local data verification unavailable"));
      else if(checked.local_data_matches)output.append(label("p","Local data metadata verified"));
      for(const error of checked.errors)output.append(label("p",error));
      if(checked.can_acknowledge) {
        const acknowledge=label("button","Mark as reviewed");output.append(acknowledge);
        acknowledge.addEventListener("click",()=>safe(async()=>{
          if(!current() || revision!==request)return;
          acknowledge.disabled=true;check.disabled=true;++request;
          try {
            await api("projects/"+project+"/git-integration-acknowledge","POST",{receipt:record.id,plan:checked.plan});
            if(current())await loadGitReceipts(offset);
          } finally {check.disabled=false;}
        }));
      }
    }));
    card.append(check,output);container.append(card);
  }
  for(const issue of result.errors)container.append(text("p",issue.path+": "+issue.error));
  if(result.truncated)container.append(label("p","Receipt scan limit reached; inspect the state directory for older records."));
  for(const [show,key,page] of [[offset>0,"Previous receipts",Math.max(0,offset-20)],[result.has_more,"Next receipts",offset+20]])if(show){const button=label("button",key);button.addEventListener("click",()=>safe(()=>loadGitReceipts(page)));container.append(button);}
}
$("git-integrations-load").addEventListener("click",()=>safe(()=>loadGitReceipts()));
async function loadGitExchange() {
  invalidateGitStorage();
  const project = selected, request = ++gitLoad;
  if (!project) return;
  const setupInput=gitSetupInput(),setupRequest=++gitSetupLoad;
  const setup=await api("projects/"+project+"/git-setup","POST",setupInput);
  if(project!==selected || request!==gitLoad || setupRequest!==gitSetupLoad || JSON.stringify(setupInput)!==JSON.stringify(gitSetupInput())) return;
  showGitSetup(project,setupInput,setup);
  if(setup.can_initialize) {clearGitReview();$("git-branches").replaceChildren();$("git-records").replaceChildren();return;}
  const [result,branches] = await Promise.all([api("projects/" + project + "/inbox", "POST", {}),api("projects/"+project+"/git-branches","POST",{})]);
  if (project !== selected || request !== gitLoad) return;
  clearGitReview();
  $("git-branches").replaceChildren();
  for (const branch of branches) {
    const row=text("div","","snapshot"), button=label("button","Review branch");
    row.append(text("p",branch.peer+" / "+branch.branch),button);
    button.addEventListener("click",()=>safe(async()=>{
      const reviewRequest=++gitReviewLoad;
      const review=await api("projects/"+project+"/git-review","POST",{peer:branch.peer,branch:branch.branch});
      if(selected!==project || request!==gitLoad || reviewRequest!==gitReviewLoad) return;
      showGitReview(project,review);
    }));
    $("git-branches").append(row);
  }
  $("git-records").replaceChildren();
  for (const item of result) {
    const card = text("div", "", "snapshot");
    card.append(text("p", item.branch + " · " + item.peer + " · " + item.head));
    if (item.lfs_status) {
      card.append(label("p", "Git LFS objects: {count} / {bytes} bytes", {count: item.lfs_status.objects, bytes: item.lfs_status.bytes}));
      if (!item.lfs_status.complete) card.append(label("p", "Waiting for Git LFS objects"));
    }
    const button = label("button", "Import tracking branch");
    button.disabled = !!item.lfs_status && !item.lfs_status.complete;
    button.addEventListener("click", () => safe(async () => {
      button.disabled = true;
      try {
        const output = await api("projects/" + project + "/import", "POST", {peer: item.peer, bundle: item.id});
        if (selected === project) {
          $("git-result").textContent = JSON.stringify(output, null, 2);
          clearGitReview();
          await loadGitExchange();
        }
      } finally { button.disabled = false; }
    }));
    card.append(button);
    $("git-records").append(card);
  }
}
function showGitReview(project,review) {
  ++gitHistoryLoad;
  const names={identical:"Branches are identical",fast_forward:"Fast-forward is possible",local_ahead:"Local branch is ahead",diverged:"Branches have diverged",unrelated:"Histories are unrelated",unborn:"No local commit yet"};
  $("git-review").replaceChildren(label("p",names[review.relation]),label("p","{local} local commits · {peer} peer commits",{local:review.local_only_commits,peer:review.peer_only_commits}));
  $("git-review-patch").textContent="";
  const integration=text("div",""), load=label("button","Load branch history"), history=text("div",""), details=text("div",""), files=text("div","");
  $("git-review").append(integration,load,history,details,files);
  showGitIntegration(project,review,integration);
  showGitMergeReview(project,review,integration);
  load.addEventListener("click",()=>safe(()=>loadGitHistory(project,review,history,details,files)));
  showGitFiles(project,review,review,files);
}
function gitContext(review) {return {peer:review.peer,branch:review.branch,base:review.base,head:review.head,local_ref:review.local_ref};}
function showGitMergeReview(project,review,container) {
  const card=text("div","","snapshot"),load=label("button","Review three-way changes"),files=text("div",""),details=text("div","");
  card.append(label("h4","Three-way Git review"),label("p","Compares committed files with their common ancestor. Proposals do not apply a repository merge or include working changes and rename detection."),load,files,details);container.append(card);
  let generation=0,fileRequest=0;
  const current=()=>selected===project && card.isConnected;
  load.addEventListener("click",()=>safe(async()=>{
    const request=++generation;++fileRequest;files.replaceChildren();details.replaceChildren();
    const result=await api("projects/"+project+"/git-merge-review","POST",gitContext(review));
    if(!current() || request!==generation)return;
    const reasons={unborn:"Three-way review needs a local commit",unrelated:"Three-way review needs a shared ancestor",multiple_bases:"Multiple merge bases require a repository merge"};
    if(!result.can_review){files.append(label("p",reasons[result.reason]));return;}
    files.append(label("p","Common ancestor"),text("p",result.merge_base));
    if(!result.files.length)files.append(label("p","No changed files"));
    const classes={local_only:"Local change only",peer_only:"Peer change only",identical:"Same change on both sides",both_changed:"Changed on both sides",structural:"Structural conflict"};
    for(const entry of result.files) {
      const row=text("div","","snapshot"),button=label("button","Preview three-way file");
      row.append(text("p",entry.path),label("p",classes[entry.classification]),button);files.append(row);
      button.addEventListener("click",()=>safe(async()=>{
        const revision=++fileRequest;details.replaceChildren();
        const draft=await api("projects/"+project+"/git-merge-file","POST",{...gitContext(review),merge_base:result.merge_base,path:entry.path});
        if(!current() || request!==generation || revision!==fileRequest)return;
        const kinds={clean:"Text proposal without conflicts",conflict:"Text conflicts detected",delete:"Proposed file deletion",manual:"Manual Git review required"};
        details.append(text("h4",entry.path),label("p",kinds[draft.kind]));
        const explanations={structure:"Review file types and path collisions in Git",file_type:"Review file types and path collisions in Git",submodule:"Review submodules in Git",symlink:"Review symbolic links in Git",large:"Three-way text previews are limited to 512 KiB per side",binary:"Binary or non-UTF-8 content needs manual review",lfs_pointer:"LFS assets need manual Git review",attributes:"Custom merge or checkout attributes need manual Git review",modify_delete:"One side modified a file deleted by the other",permissions:"File permissions need manual Git review",output_limit:"Merge proposal exceeds its preview bound"};
        if(draft.kind==="manual")details.append(label("p",explanations[draft.reason]));
        if(draft.conflicts!==null)details.append(text("p",t("Conflict regions reported")+": "+draft.conflicts));
        const tabs=text("div",""),content=text("pre","");details.append(tabs,content);
        let first=true;
        for(const [key,title] of [["candidate","Merge proposal"],["ancestor","Common ancestor"],["local","Local committed version"],["peer","Peer committed version"]]) {
          const value=key==="candidate"?draft.candidate:draft.texts[key];if(value===null || value===undefined)continue;
          const tab=label("button",title);tabs.append(tab);tab.addEventListener("click",()=>{content.textContent=value;});
          if(first){content.textContent=value;first=false;}
        }
      }));
    }
  }));
}
function showGitIntegration(project,review,container) {
  const merging=review.relation==="diverged",route=merging?"git-merge":"git-integrate";
  const card=text("div","","snapshot"), preview=label("button",merging?"Preview repository merge":"Preview integration"), apply=label("button",merging?"Apply reviewed merge":"Apply fast-forward"), output=text("div","");
  apply.disabled=true;
  const hint=text("p",""),editor=text("div","");
  card.append(label("h4",merging?"Merge diverged branch":"Integrate imported branch"),label("p",merging?"Creates a reviewed two-parent merge commit. Choose committed files or edit conflict text, then preview again. Unrelated local data is preserved.":"Updates the local branch, index and tracked files. Unrelated local files are preserved; LFS and checkout filters use separate extraction."),text("p",review.head),preview,apply,hint,output,editor);
  container.append(card);
  let plan=null,request=0,editing=false,conflictFiles=[];const choices=new Map(),drafts=new Map();
  const current=()=>selected===project && card.isConnected;
  const lockOutput=locked=>{for(const control of output.querySelectorAll("select,button"))control.disabled=locked || control.dataset.canEdit==="false";};
  preview.addEventListener("click",()=>safe(async()=>{
    plan=null;apply.disabled=true;const generation=++request;
    const query=gitContext(review);
    if(merging && choices.size)query.resolutions=Array.from(choices,([path,choice])=>({path,...choice}));
    const result=await api("projects/"+project+"/"+route,"POST",query);
    if(!current() || generation!==request)return;
    output.replaceChildren();hint.textContent="";
    if(merging){
      if(result.result_tree)output.append(label("p","Reviewed result tree"),text("p",result.result_tree));
      if(result.merge_identity)output.append(label("p","Merge author"),text("p",result.merge_identity.name+" <"+result.merge_identity.email+">"));
      if(result.conflict_files.length)conflictFiles=result.conflict_files;
      if(conflictFiles.length){
        output.append(label("p","Repository merge conflicts"),label("p","Each choice replaces the entire file, including nonconflicting changes. A missing version deletes it."));
        for(const file of conflictFiles){
          const row=text("label",""),select=document.createElement("select");
          for(const [value,title] of [["","Leave unresolved"],["local",file.local?"Use local file":"Use local deletion"],["peer",file.peer?"Use peer file":"Use peer deletion"]]){
            const option=label("option",title);option.value=value;select.append(option);
          }
          if(drafts.has(file.path)){const option=label("option","Use edited text");option.value="edited";select.append(option);}
          select.value=choices.get(file.path)?.side||"";
          select.addEventListener("change",()=>{
            if(select.value)choices.set(file.path,select.value==="edited"?{side:"edited",content:drafts.get(file.path)}:{side:select.value});else choices.delete(file.path);
            plan=null;apply.disabled=true;++request;hint.textContent=t("Choices changed; preview again");
          });
          const edit=label("button","Edit merged text");edit.type="button";edit.disabled=!file.editable;edit.dataset.canEdit=String(file.editable);
          if(!file.editable)edit.title=t("Text editing requires matching file permissions");
          edit.addEventListener("click",()=>safe(async()=>{
            if(editing || !current())return;
            editing=true;lockOutput(true);preview.disabled=true;apply.disabled=true;plan=null;const generation=++request;
            let shown=false;
            const close=()=>{editing=false;editor.replaceChildren();lockOutput(false);preview.disabled=false;hint.textContent=t("Choices changed; preview again");};
            try{
              const draft=await api("projects/"+project+"/git-merge-file","POST",{...gitContext(review),merge_base:result.merge_base,path:file.path});
              if(!current() || generation!==request)return;
              editor.replaceChildren(label("h4","Edit merged text"),text("p",file.path),label("p","Remove conflict markers and review the final text. Maximum 512 KiB; edits are saved in the private receipt."));
              for(const [key,title] of [["ancestor","Common ancestor"],["local","Local committed version"],["peer","Peer committed version"]]){
                const details=document.createElement("details"),source=document.createElement("textarea");source.value=draft.texts[key]||"";source.readOnly=true;source.rows=6;
                details.append(label("summary",title),source);editor.append(details);
              }
              const input=document.createElement("textarea"),count=text("p",""),use=label("button","Use edited text"),cancel=label("button","Cancel edit");input.rows=14;
              input.value=drafts.has(file.path)?drafts.get(file.path):typeof draft.candidate==="string"?draft.candidate:draft.texts[draft.local_info.exists?"local":"peer"];
              const update=()=>{const bytes=new TextEncoder().encode(input.value).length;count.textContent=t("Edited text bytes")+": "+bytes;use.disabled=bytes>512*1024;};
              input.addEventListener("input",update);update();
              if(draft.regions.available){
                const panel=document.createElement("details"),regionChoices=Array(draft.regions.count).fill(""),build=label("button","Build draft from selected regions");
                panel.append(label("summary","Choose conflict regions"),label("p","Building a draft replaces the editor text. Review it before use."));build.disabled=true;
                const controls=[];let composing=false;
                for(const part of draft.regions.segments){
                  if(part.kind!=="conflict")continue;
                  const region=document.createElement("details"),pick=document.createElement("select");
                  region.append(text("summary",t("Conflict region")+" "+(part.id+1)));
                  for(const [key,title] of [["ancestor","Common ancestor"],["local","Local committed version"],["peer","Peer committed version"]]){
                    const version=document.createElement("details"),value=document.createElement("textarea");value.readOnly=true;value.rows=5;value.value=part[key];
                    version.append(label("summary",title),value);region.append(version);
                  }
                  for(const [value,title] of [["","Leave unresolved"],["local","Use local region"],["peer","Use peer region"],["both","Use both (local then peer)"]]){
                    const option=label("option",title);option.value=value;pick.append(option);
                  }
                  pick.addEventListener("change",()=>{regionChoices[part.id]=pick.value;build.disabled=composing || regionChoices.some(value=>!value);});
                  controls.push(pick);region.append(pick);panel.append(region);
                }
                build.addEventListener("click",()=>safe(async()=>{
                  composing=true;build.disabled=true;input.disabled=true;use.disabled=true;for(const control of controls)control.disabled=true;
                  try{
                    const composed=await api("projects/"+project+"/git-merge-file","POST",{...gitContext(review),merge_base:draft.merge_base,path:file.path,region_token:draft.regions.token,region_choices:regionChoices});
                    if(current() && generation===request && editing && input.isConnected)input.value=composed.composed_text;
                  }finally{composing=false;input.disabled=false;update();for(const control of controls)control.disabled=false;build.disabled=regionChoices.some(value=>!value);}
                }));
                panel.append(build);editor.append(panel);
              }else if(draft.kind==="conflict")editor.append(label("p","Region choices are unavailable for this proposal; edit the text manually."));
              use.addEventListener("click",()=>{
                drafts.set(file.path,input.value);choices.set(file.path,{side:"edited",content:input.value});
                if(!Array.from(select.options).some(option=>option.value==="edited")){const option=label("option","Use edited text");option.value="edited";select.append(option);}
                select.value="edited";close();
              });
              cancel.addEventListener("click",close);editor.append(input,count,use,cancel);shown=true;
            }finally{if(!shown){editing=false;preview.disabled=false;lockOutput(false);}}
          }));
          row.append(text("span",file.path),select);output.append(row,edit);
        }
      }
    }
    if(result.local_data)output.append(label("p","Local files preserved: {files} · {bytes} bytes",result.local_data));
    if(result.errors.length){output.append(label("p","Integration blocked"));for(const reason of result.errors)output.append(label("p",reason));}
    if(result.can_integrate){plan={input:query,token:result.plan};apply.disabled=false;}
  }));
  apply.addEventListener("click",()=>safe(async()=>{
    const saved=plan;if(!saved || !current())return;
    plan=null;apply.disabled=true;preview.disabled=true;++request;
    lockOutput(true);
    try {
      const result=await api("projects/"+project+"/"+route,"POST",{...saved.input,apply:true,plan:saved.token});
      if(!current())return;
      $("git-result").textContent=t("Integration complete")+"\n"+(result.result_commit?result.result_commit+"\n":"")+result.receipt_path;
      await loadGitReceipts();
      await loadGitExchange();
    } finally {preview.disabled=false;lockOutput(false);}
  }));
}
function showGitExtraction(project,review,comparison,container) {
  const card=text("div","","snapshot"), form=document.createElement("form"), destination=document.createElement("input"), lfs=document.createElement("input");
  destination.required=true; lfs.type="checkbox";lfs.checked=true;
  const destinationLabel=text("label",""), lfsLabel=text("label","");
  destinationLabel.append(label("span","New extraction directory"),destination);lfsLabel.append(lfs,label("span","Expand verified LFS assets"));
  const preview=label("button","Preview extraction"), apply=label("button","Extract to new directory"), output=text("div","");
  apply.type="button";apply.disabled=true;
  form.append(destinationLabel,lfsLabel,preview,apply);
  card.append(label("h4","Extract Git revision"),label("p","Copies the complete selected revision. Local Git files and changes are preserved."),text("p",comparison.commit?comparison.commit.oid:review.head),form,output);container.append(card);
  card.insertBefore(label("p","File snapshot limits: 1 GiB per Git blob, 4 GiB per LFS asset, 16 GiB total."),form);
  let plan=null,request=0;
  const input=()=>({...gitContext(review),commit:comparison.commit?comparison.commit.oid:review.head,destination:destination.value,lfs:lfs.checked});
  const current=()=>selected===project && card.isConnected;
  form.addEventListener("input",()=>{plan=null;++request;apply.disabled=true;output.replaceChildren();});
  form.addEventListener("submit",event=>{
    event.preventDefault();safe(async()=>{
      const query=input(),generation=++request;plan=null;apply.disabled=true;
      const result=await api("projects/"+project+"/git-extract","POST",query);
      if(!current() || generation!==request || JSON.stringify(query)!==JSON.stringify(input())) return;
      output.replaceChildren(label("p","{files} files · {bytes} bytes · {assets} LFS files",{files:result.file_count,bytes:result.bytes,assets:result.lfs_files}));
      if(result.errors.length)output.append(label("p","Extraction blocked"),text("pre",JSON.stringify(result.errors,null,2)));
      if(result.can_extract){plan={input:query,token:result.plan};apply.disabled=false;}
    });
  });
  apply.addEventListener("click",()=>safe(async()=>{
    const saved=plan;if(!saved || !current())return;
    plan=null;apply.disabled=true;const generation=++request;
    const result=await api("projects/"+project+"/git-extract","POST",{...saved.input,apply:true,plan:saved.token});
    if(!current() || generation!==request)return;
    output.replaceChildren(label("p","Extraction complete"),text("p",result.destination),text("p",result.receipt_path));
  }));
}
function showGitFiles(project,review,comparison,container) {
  container.replaceChildren(); $("git-review-patch").textContent=""; ++gitReviewLoad;
  showGitExtraction(project,review,comparison,container);
  if(!comparison.files.length) container.append(label("p","No changed files"));
  for (const file of comparison.files) {
    const row=text("div","","snapshot"), button=label("button","Review file");
    row.append(text("p",file.status+" / "+file.path),button);
    button.addEventListener("click",()=>safe(async()=>{
      const request=++gitReviewLoad;
      const input={...gitContext(review),path:file.path};
      if(comparison.commit) Object.assign(input,{commit:comparison.commit.oid,parent:comparison.parent});
      const result=await api("projects/"+project+(comparison.commit?"/git-commit-file":"/git-review-file"),"POST",input);
      if(selected!==project || request!==gitReviewLoad) return;
      $("git-review-patch").textContent=result.binary?t(result.message):(result.reason==="lfs_pointer"?t("LFS pointer comparison; asset bytes are not loaded.")+"\n\n":"")+result.patch;
    }));
    container.append(row);
  }
}
async function loadGitHistory(project,review,container,details,files,offset=0) {
  const request=++gitHistoryLoad;
  const result=await api("projects/"+project+"/git-history","POST",{...gitContext(review),offset});
  if(selected!==project || request!==gitHistoryLoad) return;
  container.replaceChildren(label("h4","Branch history")); details.replaceChildren();
  showGitFiles(project,review,review,files);
  const previous=label("button","Previous page"), next=label("button","Next page");
  previous.disabled=result.offset===0; next.disabled=result.next_offset===null;
  previous.addEventListener("click",()=>safe(()=>loadGitHistory(project,review,container,details,files,Math.max(0,result.offset-result.limit))));
  next.addEventListener("click",()=>safe(()=>loadGitHistory(project,review,container,details,files,result.next_offset)));
  container.append(previous,next);
  if(result.truncated) container.append(label("p","History page limit reached"));
  const list=text("div","","git-commit-list");container.append(list);
  for(const entry of result.commits) {
    const row=text("div","","snapshot"), button=label("button","Review commit");
    row.append(text("p",entry.oid.slice(0,12)+" / "+entry.subject),text("p",entry.author.name+" / "+entry.author.date),button);
    button.addEventListener("click",()=>safe(()=>loadGitCommit(project,review,entry.oid,details,files,request)));
    list.append(row);
  }
}
async function loadGitCommit(project,review,oid,details,files,historyRequest,parent) {
  const request=++gitReviewLoad, input={...gitContext(review),commit:oid};
  if(parent!==undefined) input.parent=parent;
  const result=await api("projects/"+project+"/git-commit","POST",input);
  if(selected!==project || request!==gitReviewLoad || historyRequest!==gitHistoryLoad) return;
  const entry=result.commit;
  details.replaceChildren(text("h4",entry.oid+" / "+entry.subject));
  for(const key of ["author","committer"]) {
    const row=text("p","");
    row.append(label("span",key==="author"?"Author":"Committer"),text("span",": "+entry[key].name+" <"+entry[key].email+">")); details.append(row);
  }
  if(!entry.parents.length) details.append(label("p","Root commit"));
  entry.parents.forEach((oid,index)=>{
    const row=text("p",""), button=label("button","Compare with parent {number}",{number:index+1});
    button.addEventListener("click",()=>safe(()=>loadGitCommit(project,review,entry.oid,details,files,historyRequest,index+1)));
    row.append(button,text("span"," "+oid)); details.append(row);
  });
  const back=label("button","Return to branch comparison");
  back.addEventListener("click",()=>{details.replaceChildren();showGitFiles(project,review,review,files);});
  details.append(back); showGitFiles(project,review,result,files);
}
$("git-refresh").addEventListener("click", () => safe(loadGitExchange));
$("git-export").addEventListener("click", () => safe(async () => {
  const project = selected, lfs = $("git-lfs").checked;
  if (!project) return;
  $("git-export").disabled = true;
  try {
    const output = await api("projects/" + project + "/export", "POST", {lfs});
    if (project !== selected) return;
    $("git-result").textContent = JSON.stringify(output, null, 2);
    await loadGitExchange();
  } finally { $("git-export").disabled = false; }
}));
async function loadTransfers() {
  const project = selected;
  if (!project) return;
  const request = ++transferLoad;
  const revision = transferLimitsRevision;
  const [result, limits] = await Promise.all([api("network/transfers", "POST", {project}), api("network/transfer-limits")]);
  if (selected !== project || request !== transferLoad) return;
  if (revision === transferLimitsRevision) showTransferLimits(limits);
  $("transfer-records").replaceChildren();
  if (!result.files.length) $("transfer-records").append(label("p", "No direct transfers"));
  for (const file of result.files) {
    const card = text("div", "", "snapshot");
    card.append(text("p", file.name + " · " + file.peer + " · " + file.id),
      label("p", file.direction === "out" ? "Outgoing transfer" : "Incoming transfer"),
      label("p", file.delivered ? "Delivered" : "Waiting for continuation"),
      text("p", file.acknowledged_bytes + " / " + file.bytes + " B"));
    const progress = document.createElement("progress");
    progress.max = file.bytes || 1;
    progress.value = file.bytes ? file.acknowledged_bytes : (file.delivered ? 1 : 0);
    card.append(progress);
    if (file.error) card.append(label("p", file.error_code === "unsupported_protocol"
      ? "This peer needs an update or legacy mode."
      : "Transfer interrupted. Check receiver, pairing and VPN, then resume."));
    if (file.direction === "out" && !file.delivered) {
      const resume = label("button", "Resume transfer");
      resume.addEventListener("click", () => safe(async () => {
        resume.disabled = true;
        try {
          const output = await api("network/transfer-resume", "POST", {project, transfer: file.id});
          if (selected !== project) return;
          showTransferResult(output);
          await loadTransfers();
        } finally { resume.disabled = false; }
      }));
      card.append(resume);
    }
    const remove = label("button", "Remove local transfer");
    remove.addEventListener("click", () => safe(async () => {
      remove.disabled = true;
      try {
        await api("network/transfer-cancel", "POST", {project, transfer: file.id, direction: file.direction});
        if (selected === project) await loadTransfers();
      } finally { remove.disabled = false; }
    }));
    card.append(remove);
    $("transfer-records").append(card);
    CySynckI18n.apply(card);
  }
  if (result.files.length) $("transfer-records").append(label("p", "Removal clears local partial data or transfer history. Received files are kept; the other peer is unchanged."));
  for (const issue of result.errors) $("transfer-records").append(text("p", issue.id + ": " + issue.error));
}
$("transfers-load").addEventListener("click", () => safe(loadTransfers));
function showTransferResult(result) {
  $("transfer-result").replaceChildren(label("p", result.delivered ? "Delivered" : "Waiting for continuation"),
    text("p", result.id));
  if (result.error) $("transfer-result").append(label("p", result.error_code === "unsupported_protocol"
    ? "This peer needs an update or legacy mode."
    : "Transfer interrupted. Check receiver, pairing and VPN, then resume."));
}
$("transfer-send").addEventListener("submit", event => {
  event.preventDefault();
  const project = selected;
  if (!project) return;
  const data = {project, peer: $("transfer-peer").value, path: $("transfer-path").value, legacy: $("transfer-legacy").checked};
  const button = event.currentTarget.querySelector("button");
  safe(async () => {
    button.disabled = true;
    try {
      const result = await api("network/send-file", "POST", data);
      if (selected !== project) return;
      showTransferResult(result);
      await loadTransfers();
    } finally { button.disabled = false; }
  });
});
$("compare-files").addEventListener("submit", event => {
  event.preventDefault();
  safe(async () => {
    const project = selected;
    if (!project) return;
    const result = await api("projects/" + project + "/compare", "POST", {left: $("compare-left").value, right: $("compare-right").value});
    if (project === selected) showDiff(result);
  });
});
$("copy-patch").addEventListener("click", () => safe(async () => {
  if (currentPatch) await navigator.clipboard.writeText(currentPatch);
}));
$("inspect-storage").addEventListener("click", () => safe(async () => {
  storagePlan = null;
  $("prune-storage").disabled = true;
  const result = await api("storage");
  storagePlan = result;
  $("storage-output").replaceChildren(
    label("div", "{snapshots} backups · {bytes} bytes stored", {snapshots: result.snapshots, bytes: result.stored_bytes}),
    label("div", "{objects} unused objects · {bytes} reclaimable bytes", {objects: result.unused_objects, bytes: result.reclaimable_bytes}),
    label("div", "{objects} missing objects", {objects: result.missing_objects}));
  $("storage-output").append(label("div", "{objects} block objects · {files} chunked file versions", {objects: result.block_objects, files: result.chunked_files}));
  if (result.content_defined_files) $("storage-output").append(label("p", "Content-defined file versions: {count}", {count:result.content_defined_files}));
  $("storage-output").append(label("div", result.deduplication_complete ? "{bytes} deduplicated bytes" : "Deduplication size unavailable", {bytes: result.deduplicated_bytes}));
  for (const error of result.errors) $("storage-output").append(text("div", (error.path || "") + ": " + error.error));
  $("prune-storage").disabled = !result.can_prune || result.unused_objects === 0;
}));
$("prune-storage").addEventListener("click", () => safe(async () => {
  if (!storagePlan) return;
  const plan = storagePlan.plan;
  storagePlan = null;
  $("prune-storage").disabled = true;
  $("storage-output").replaceChildren(label("div", "Cleanup in progress…"));
  try {
    const result = await api("storage/prune", "POST", {apply: true, plan});
    $("storage-output").replaceChildren(label("div", "{objects} objects removed · {bytes} bytes freed", {objects: result.removed_objects, bytes: result.reclaimed_bytes}));
    for (const error of result.failures) $("storage-output").append(text("div", error.error));
  } catch (error) {
    $("storage-output").replaceChildren(label("div", "Inspect storage again before retrying."));
    throw error;
  }
}));
document.querySelectorAll("[data-action]").forEach(button => button.addEventListener("click", () => safe(() => action(button.dataset.action))));
$("create").addEventListener("submit", event => { event.preventDefault(); safe(async () => { const data = Object.fromEntries(new FormData(event.target)); const project = await api("projects", "POST", data); selected = project.id; event.target.reset(); await refresh(); }); });
if (token) safe(refresh);
