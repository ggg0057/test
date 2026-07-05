const $ = (selector) => document.querySelector(selector);
const state = { clientId: "", name: "", lastId: 0, polling: false, divider: false };
const els = {
  modal: $("#loginModal"), loginForm: $("#loginForm"), nameInput: $("#nameInput"), loginError: $("#loginError"),
  messageList: $("#messageList"), messageForm: $("#messageForm"), messageInput: $("#messageInput"),
  memberList: $("#memberList"), memberCount: $("#memberCount"), membersButton: $("#membersButton"),
  membersPanel: $("#membersPanel"), connectionState: $("#connectionState"), toast: $("#toast"),
};

async function api(path, options = {}) {
  const response = await fetch(path, { headers: { "Content-Type": "application/json", ...(options.headers || {}) }, ...options });
  const data = await response.json().catch(() => ({}));
  if (!response.ok) throw new Error(data.error || `请求失败 (${response.status})`);
  return data;
}

function toast(message) {
  els.toast.textContent = message;
  els.toast.classList.add("show");
  clearTimeout(toast.timer);
  toast.timer = setTimeout(() => els.toast.classList.remove("show"), 2200);
}

function initials(name) { return Array.from(name).slice(0, 2).join("").toUpperCase(); }

function avatar(name, bot = false) {
  const node = document.createElement("span");
  node.className = "avatar";
  node.textContent = bot ? "译" : initials(name);
  return node;
}

function renderMessage(message) {
  if (!state.divider) {
    const divider = document.createElement("p");
    divider.className = "system-message";
    divider.textContent = "— 今天 —";
    els.messageList.append(divider);
    state.divider = true;
  }
  if (message.kind === "system") {
    const line = document.createElement("p");
    line.className = "system-message";
    line.textContent = `— ${message.text} · ${message.time} —`;
    els.messageList.append(line);
    return;
  }
  const mine = message.kind === "user" && message.name === state.name;
  const article = document.createElement("article");
  article.className = `message${mine ? " mine" : ""}${message.kind === "bot" ? " bot" : ""}`;
  article.append(avatar(message.name, message.kind === "bot"));
  const copy = document.createElement("div");
  copy.className = "message-copy";
  const head = document.createElement("div");
  head.className = "message-head";
  const author = document.createElement("strong");
  author.textContent = mine ? `${message.name}（我）` : message.name;
  const time = document.createElement("time");
  time.textContent = message.time;
  head.append(author, time);
  const text = document.createElement("div");
  text.className = "message-text";
  text.textContent = message.text;
  copy.append(head, text);
  article.append(copy);
  els.messageList.append(article);
}

function renderMembers(users) {
  els.memberList.replaceChildren();
  users.forEach((user) => {
    const row = document.createElement("div");
    row.className = "member";
    row.append(avatar(user.name, user.bot));
    const copy = document.createElement("span");
    const name = document.createElement("strong");
    name.textContent = user.name === state.name ? `${user.name}（我）` : user.name;
    const status = document.createElement("small");
    status.textContent = user.bot ? "词典机器人" : "在线";
    copy.append(name, status);
    row.append(copy);
    els.memberList.append(row);
  });
  els.memberCount.textContent = String(users.length);
}

async function poll() {
  if (!state.clientId || state.polling) return;
  state.polling = true;
  try {
    const params = new URLSearchParams({ clientId: state.clientId, after: String(state.lastId) });
    const data = await api(`/api/messages?${params}`);
    const nearBottom = els.messageList.scrollHeight - els.messageList.scrollTop - els.messageList.clientHeight < 100;
    data.messages.forEach(renderMessage);
    state.lastId = data.lastId;
    renderMembers(data.users);
    if (nearBottom || data.messages.some((item) => item.name === state.name)) els.messageList.scrollTop = els.messageList.scrollHeight;
    els.connectionState.innerHTML = "<i></i> C++ 服务在线";
  } catch (error) {
    els.connectionState.textContent = "正在重新连接";
    if (error.message.includes("登录已失效")) location.reload();
  } finally { state.polling = false; }
}

els.loginForm.addEventListener("submit", async (event) => {
  event.preventDefault();
  const submit = event.submitter;
  submit.disabled = true;
  els.loginError.textContent = "";
  try {
    const data = await api("/api/join", { method: "POST", body: JSON.stringify({ name: els.nameInput.value }) });
    state.clientId = data.clientId;
    state.name = data.name;
    els.modal.classList.add("hidden");
    await poll();
    els.messageInput.focus();
  } catch (error) { els.loginError.textContent = error.message; }
  finally { submit.disabled = false; }
});

els.messageForm.addEventListener("submit", async (event) => {
  event.preventDefault();
  const text = els.messageInput.value.trim();
  if (!text || !state.clientId) return;
  const button = event.submitter || els.messageForm.querySelector("button");
  button.disabled = true;
  try {
    await api("/api/messages", { method: "POST", body: JSON.stringify({ clientId: state.clientId, text }) });
    els.messageInput.value = "";
    els.messageInput.style.height = "auto";
    await poll();
  } catch (error) { toast(error.message); }
  finally { button.disabled = false; els.messageInput.focus(); }
});

els.messageInput.addEventListener("input", () => {
  els.messageInput.style.height = "auto";
  els.messageInput.style.height = `${Math.min(els.messageInput.scrollHeight, 100)}px`;
});
els.messageInput.addEventListener("keydown", (event) => {
  if (event.key === "Enter" && !event.shiftKey) { event.preventDefault(); els.messageForm.requestSubmit(); }
});
els.membersButton.addEventListener("click", () => els.membersPanel.classList.toggle("open"));
window.addEventListener("beforeunload", () => {
  if (state.clientId) navigator.sendBeacon("/api/leave", new Blob([JSON.stringify({ clientId: state.clientId })], { type: "application/json" }));
});
setInterval(poll, 1000);
