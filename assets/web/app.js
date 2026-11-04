"use strict";
const $ = id => document.getElementById(id);
const {t, setText: translated, init: initLanguages} = CySynckI18n;
initLanguages();
let token = "", selected = "", transferLoad = 0, gitLoad = 0;
try { token = sessionStorage.getItem("cysynck-token") || ""; } catch (_) { /* Storage may be disabled. */ }
let storagePlan = null, servicePreview = null;
let backupStorageDirty = false;
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
    backupStorageDirty = false;
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
    $("git-result").replaceChildren();
    ++gitLoad;
    $("transfer-result").replaceChildren();
    $("transfer-peer").value = "";
    $("transfer-path").value = "";
    $("transfer-legacy").checked = false;
    ++transferLoad;
  }
  selected = project.id;
  selectedProject = project;
  $("git-panel").hidden = project.mode !== "git-bundles";
  $("recovery-panel").hidden = false;
  $("received-panel").hidden = false;
  $("transfer-panel").hidden = false;
  $("resolution-pause").disabled = project.paused;
  $("resolution-apply").disabled = !resolutionPreview || !resolutionPreview.spaceAvailable || !project.paused || !!project.resolution_pending;
  $("detail").hidden = false;
  $("project-name").textContent = project.name;
  $("project-root").textContent = project.root + " · " + project.mode;
  if (!backupStorageDirty) $("backup-blocks").checked = (project.backup_storage || "auto") === "auto";
  const snapshots = await api("projects/" + selected + "/snapshots");
  if (selected !== project.id) return;
  $("snapshots").replaceChildren(...snapshots.map(s => {
    const card = text("div", "", "snapshot");
    card.append(label("div", "{created} · {count} files · {id}", {created: s.created.slice(0, 19), count: s.file_count, id: s.id}));
    if (s.block_storage) card.append(label("p", "Block data: {written} bytes written · {reused} bytes reused", {written: s.block_storage.new_block_bytes, reused: s.block_storage.reused_block_bytes}));
    return card;
  }));
  const revision = transferLimitsRevision;
  const limits = await api("network/transfer-limits");
  if (selected === project.id && revision === transferLimitsRevision) showTransferLimits(limits);
}
$("backup-blocks").addEventListener("change", () => {
  backupStorageDirty = true;
  $("backup-storage-result").replaceChildren();
});
$("backup-storage-form").addEventListener("submit", event => {
  event.preventDefault();
  const project = selected, enabled = $("backup-blocks").checked;
  if (!project) return;
  const button = event.currentTarget.querySelector("button");
  safe(async () => {
    button.disabled = true;
    try {
      const updated = await api("projects/" + project, "PUT", {backup_storage: enabled ? "auto" : "whole"});
      if (selected !== project || $("backup-blocks").checked !== enabled) return;
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
async function loadGitExchange() {
  const project = selected, request = ++gitLoad;
  if (!project) return;
  const result = await api("projects/" + project + "/inbox", "POST", {});
  if (project !== selected || request !== gitLoad) return;
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
        if (selected === project) $("git-result").textContent = JSON.stringify(output, null, 2);
      } finally { button.disabled = false; }
    }));
    card.append(button);
    $("git-records").append(card);
  }
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
