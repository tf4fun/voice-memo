"use strict";

document.querySelectorAll("time[datetime]").forEach((node) => {
  node.textContent = new Intl.DateTimeFormat("zh-CN", {year:"numeric", month:"2-digit", day:"2-digit", hour:"2-digit", minute:"2-digit", hour12:false}).format(new Date(node.dateTime));
});
document.getElementById("refresh")?.addEventListener("click", () => location.reload());

const detail = document.querySelector("main[data-memo-id]");
if (detail) {
  const id = detail.dataset.memoId;
  const api = `/api/v1/memos/${encodeURIComponent(id)}`;
  const notice = document.getElementById("notice");
  const editor = document.getElementById("editor");
  const draft = document.getElementById("draft");
  const transcript = document.getElementById("transcript");
  const edit = document.getElementById("edit");
  const requeue = document.getElementById("transcribe");
  const badge = document.getElementById("memo-status");
  const confirmDialog = document.getElementById("confirmation");
  let saved = draft.value, editing = false, busy = false, pollTimer, epoch = 0;

  function message(text, error = false) {
    notice.textContent = text;
    notice.hidden = !text;
    notice.classList.toggle("error", error);
  }
  function pending() { return ["uploaded", "transcribing"].includes(detail.dataset.status); }
  function lock(value) {
    busy = value;
    if (value) epoch++;
    for (const key of ["edit", "save", "cancel-edit", "delete"]) document.getElementById(key).disabled = value;
    requeue.disabled = value || pending();
    draft.disabled = value;
  }
  async function request(path, method, body) {
    const response = await fetch(path, {method, credentials:"same-origin", headers:body ? {"Content-Type":"application/json"} : {}, body:body ? JSON.stringify(body) : undefined});
    if (!response.ok) {
      if (response.status === 404) throw new Error("录音已不存在，请返回录音档案。");
      throw new Error("操作失败，请稍后重试。你的修改仍保留在页面中。");
    }
    return response.status === 204 ? null : response.json();
  }
  function render(memo) {
    detail.dataset.status = memo.status;
    badge.className = `badge ${memo.status}`;
    badge.textContent = {uploaded:"等待转写", transcribing:"转写中", ready:"已转写", failed:"转写失败"}[memo.status] || "未知状态";
    if (!editing) {
      saved = memo.transcript || "";
      draft.value = saved;
      transcript.textContent = saved;
      document.getElementById("transcript-empty").hidden = Boolean(saved);
    }
    const error = document.getElementById("asr-error");
    error.hidden = !memo.error;
    error.textContent = memo.error ? `转写失败：${memo.error}` : "";
    requeue.disabled = busy || pending();
    schedule();
  }
  function schedule() {
    clearTimeout(pollTimer);
    if (!pending()) return;
    pollTimer = setTimeout(async () => {
      if (!editing && !busy && !document.hidden) {
        const started = epoch;
        try {
          const memo = await request(api, "GET");
          if (started === epoch && !editing && !busy) render(memo);
        } catch { if (started === epoch) message("状态更新失败，可稍后刷新页面重试。", true); }
      }
      schedule();
    }, 4000);
  }
  function setEditing(value) {
    epoch++;
    editing = value;
    editor.hidden = !value;
    transcript.hidden = value;
    edit.hidden = value;
    document.getElementById("transcript-empty").hidden = value || Boolean(saved);
    if (value) { draft.value = saved; draft.focus(); }
  }
  function confirmAction(title, description) {
    document.getElementById("confirm-title").textContent = title;
    document.getElementById("confirm-description").textContent = description;
    confirmDialog.returnValue = "cancel";
    confirmDialog.showModal();
    return new Promise((resolve) => confirmDialog.addEventListener("close", () => resolve(confirmDialog.returnValue === "confirm"), {once:true}));
  }
  edit.addEventListener("click", () => { message(""); setEditing(true); });
  document.getElementById("cancel-edit").addEventListener("click", () => { setEditing(false); message(""); schedule(); });
  editor.addEventListener("submit", async (event) => {
    event.preventDefault();
    if (busy) return;
    lock(true);
    try {
      const memo = await request(api, "PATCH", {transcript:draft.value});
      setEditing(false); render(memo); message("转录文字已保存。");
    } catch (error) { message(error.message, true); }
    finally { lock(false); }
  });
  requeue.addEventListener("click", async () => {
    if (busy || !(await confirmAction("重新转写这条录音？", "新的转写结果会替换现有文字，包括手动修改的内容。"))) return;
    lock(true);
    try {
      await request(`${api}/transcribe`, "POST");
      setEditing(false); render(await request(api, "GET")); message("已加入转写队列，完成后会自动更新。");
    } catch (error) { message(error.message, true); }
    finally { lock(false); }
  });
  document.getElementById("delete").addEventListener("click", async () => {
    if (busy || !(await confirmAction("删除这条录音？", "音频和转录文字将被删除，无法恢复。"))) return;
    lock(true);
    try { await request(api, "DELETE"); editing = false; location.assign("/"); }
    catch (error) { message(error.message, true); lock(false); }
  });
  document.getElementById("audio")?.addEventListener("error", () => message("音频暂时无法播放，可能正在处理另一段音频。请稍后刷新重试，或下载音频。", true));
  window.addEventListener("beforeunload", (event) => {
    if (editing && draft.value !== saved) { event.preventDefault(); event.returnValue = ""; }
  });
  schedule();
}
