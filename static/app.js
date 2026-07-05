const $ = (selector) => document.querySelector(selector);
const $$ = (selector) => Array.from(document.querySelectorAll(selector));

const VIEW_META = {
  chat: ["PUBLIC LOUNGE", "公共大厅", "一个安静、友好的共享空间"],
  dictionary: ["PERSONAL DICTIONARY", "独立查词", "查询结果会自动保存到本次查词记录"],
  history: ["MEMORY ARCHIVE", "记录查询", "按关键词检索聊天与查词历史"],
  music: ["ONLINE MUSIC", "在线音乐", "选择曲库中的音乐，随时开始或暂停"],
  game: ["WORD RAIN", "单词雨挑战", "在线运行的词义挑战与共享排行榜"],
};

const state = {
  clientId: "",
  name: "",
  lastId: 0,
  polling: false,
  renderedDivider: false,
  view: "chat",
  historyType: "chat",
  currentTrack: "",
  authMode: "guest",
  accountMode: "guest",
  roomId: "lobby",
  roomName: "公共大厅",
  recipient: "",
  loadingChannels: false,
  channelTick: 0,
  channelSignature: "",
};

const els = {
  loginModal: $("#loginModal"), loginForm: $("#loginForm"), nameInput: $("#nameInput"), loginError: $("#loginError"),
  passwordInput: $("#passwordInput"), passwordGroup: $("#passwordGroup"), authDescription: $("#authDescription"),
  authSubmit: $("#authSubmit"), nameLabel: $("#nameLabel"),
  profileName: $("#profileName"), profileAvatar: $("#profileAvatar"), leaveButton: $("#leaveButton"),
  viewEyebrow: $("#viewEyebrow"), viewTitle: $("#viewTitle"), viewSubtitle: $("#viewSubtitle"),
  messageList: $("#messageList"), messageForm: $("#messageForm"), messageInput: $("#messageInput"),
  messageCounter: $("#messageCounter"), commandButton: $("#commandButton"), botShortcut: $("#botShortcut"),
  memberList: $("#memberList"), memberCount: $("#memberCount"), roomCount: $("#roomCount"),
  membersPanel: $("#membersPanel"), membersToggle: $("#membersToggle"), banner: $("#connectionBanner"),
  dictionaryForm: $("#dictionaryForm"), dictionaryInput: $("#dictionaryInput"), dictionaryResult: $("#dictionaryResult"),
  resultWord: $("#resultWord"), resultMeaning: $("#resultMeaning"), resultTime: $("#resultTime"),
  recentLookupList: $("#recentLookupList"), historyForm: $("#historyForm"), historyInput: $("#historyInput"),
  historyList: $("#historyList"), historyCount: $("#historyCount"), clearHistorySearch: $("#clearHistorySearch"),
  quickHistory: $("#quickHistory"), themeButton: $("#themeButton"), sceneShortcut: $("#sceneShortcut"),
  themeDrawer: $("#themeDrawer"), closeThemeDrawer: $("#closeThemeDrawer"), serverDetail: $("#serverDetail"),
  toast: $("#toast"),
  audioPlayer: $("#audioPlayer"), musicList: $("#musicList"), currentTrackName: $("#currentTrackName"),
  playerStatus: $("#playerStatus"), playPauseButton: $("#playPauseButton"), musicProgress: $("#musicProgress"),
  currentTime: $("#currentTime"), durationTime: $("#durationTime"), volumeControl: $("#volumeControl"),
  refreshMusic: $("#refreshMusic"), adminPassword: $("#adminPassword"), musicFile: $("#musicFile"),
  uploadMusic: $("#uploadMusic"),
  gameScore: $("#gameScore"), gameCombo: $("#gameCombo"), gameLives: $("#gameLives"),
  gameTime: $("#gameTime"), gameWord: $("#gameWord"), gameHint: $("#gameHint"),
  gameAnswers: $("#gameAnswers"), gameStart: $("#gameStart"), gamePause: $("#gamePause"),
  scoreList: $("#scoreList"), refreshScores: $("#refreshScores"),
  transparencyToggle: $("#transparencyToggle"), fireworksToggle: $("#fireworksToggle"),
  fireworksCanvas: $("#fireworksCanvas"),
  roomList: $("#roomList"), contactList: $("#contactList"), addRoomButton: $("#addRoomButton"),
};

async function api(path, options = {}) {
  const response = await fetch(path, {
    headers: { "Content-Type": "application/json", ...(options.headers || {}) },
    ...options,
  });
  const data = await response.json().catch(() => ({}));
  if (!response.ok) throw new Error(data.error || `请求失败 (${response.status})`);
  return data;
}

function showToast(message) {
  els.toast.textContent = message;
  els.toast.classList.add("show");
  clearTimeout(showToast.timer);
  showToast.timer = setTimeout(() => els.toast.classList.remove("show"), 2200);
}

function initials(name) {
  return Array.from(name.trim()).slice(0, 2).join("").toUpperCase() || "?";
}

function createAvatar(name, bot = false) {
  const avatar = document.createElement("span");
  avatar.className = `avatar${bot ? " bot" : ""}`;
  avatar.textContent = bot ? "译" : initials(name);
  return avatar;
}

function setScene(scene) {
  const normalized = String(scene).padStart(2, "0");
  document.documentElement.style.setProperty("--scene-image", `url('/images/scene-${normalized}.jpg')`);
  localStorage.setItem("gensokyo-scene", normalized);
  $$("[data-scene]").forEach((button) => button.classList.toggle("active", button.dataset.scene === normalized));
}

function toggleThemeDrawer(open) {
  els.themeDrawer.classList.toggle("open", open);
  els.themeDrawer.setAttribute("aria-hidden", String(!open));
}

function setTransparency(enabled) {
  document.body.classList.toggle("transparent-window", enabled);
  els.transparencyToggle.checked = enabled;
  localStorage.setItem("gensokyo-transparency", enabled ? "1" : "0");
}

const fireworks = {
  enabled: false,
  context: els.fireworksCanvas.getContext("2d"),
  particles: [],
  frame: 0,
  timer: 0,
};

function resizeFireworks() {
  const ratio = Math.min(window.devicePixelRatio || 1, 2);
  els.fireworksCanvas.width = Math.round(window.innerWidth * ratio);
  els.fireworksCanvas.height = Math.round(window.innerHeight * ratio);
  fireworks.context.setTransform(ratio, 0, 0, ratio, 0, 0);
}

function launchFirework(x = window.innerWidth * (.15 + Math.random() * .7), y = window.innerHeight * (.12 + Math.random() * .42)) {
  if (!fireworks.enabled) return;
  const colors = ["#d9f15b", "#ff8b79", "#8be5ec", "#ffd86b", "#d7a7ff", "#ffffff"];
  const color = colors[Math.floor(Math.random() * colors.length)];
  const count = 42 + Math.floor(Math.random() * 22);
  for (let index = 0; index < count; index++) {
    const angle = Math.PI * 2 * index / count + Math.random() * .08;
    const speed = 1.5 + Math.random() * 3.8;
    fireworks.particles.push({
      x, y, previousX: x, previousY: y,
      vx: Math.cos(angle) * speed, vy: Math.sin(angle) * speed,
      life: 58 + Math.random() * 25, age: 0, color,
    });
  }
}

function animateFireworks() {
  if (!fireworks.enabled) return;
  const context = fireworks.context;
  context.clearRect(0, 0, window.innerWidth, window.innerHeight);
  fireworks.particles = fireworks.particles.filter((particle) => {
    particle.age += 1;
    if (particle.age >= particle.life) return false;
    particle.previousX = particle.x;
    particle.previousY = particle.y;
    particle.vx *= .986;
    particle.vy = particle.vy * .986 + .035;
    particle.x += particle.vx;
    particle.y += particle.vy;
    const alpha = Math.max(0, 1 - particle.age / particle.life);
    context.beginPath();
    context.moveTo(particle.previousX, particle.previousY);
    context.lineTo(particle.x, particle.y);
    context.strokeStyle = particle.color;
    context.globalAlpha = alpha;
    context.lineWidth = 1.4;
    context.stroke();
    return true;
  });
  context.globalAlpha = 1;
  fireworks.frame = requestAnimationFrame(animateFireworks);
}

function setFireworks(enabled) {
  fireworks.enabled = enabled;
  els.fireworksToggle.checked = enabled;
  els.fireworksCanvas.classList.toggle("enabled", enabled);
  localStorage.setItem("gensokyo-fireworks", enabled ? "1" : "0");
  clearInterval(fireworks.timer);
  cancelAnimationFrame(fireworks.frame);
  fireworks.particles = [];
  fireworks.context.clearRect(0, 0, window.innerWidth, window.innerHeight);
  if (!enabled) return;
  resizeFireworks();
  launchFirework(window.innerWidth * .3, window.innerHeight * .28);
  setTimeout(() => launchFirework(window.innerWidth * .68, window.innerHeight * .2), 280);
  fireworks.timer = setInterval(launchFirework, 1450);
  fireworks.frame = requestAnimationFrame(animateFireworks);
}

function setView(view) {
  if (!VIEW_META[view]) return;
  state.view = view;
  const meta = VIEW_META[view];
  els.viewEyebrow.textContent = view === "chat" && state.recipient ? "DIRECT MESSAGE" : meta[0];
  els.viewTitle.textContent = view === "chat" ? (state.recipient || state.roomName) : meta[1];
  els.viewSubtitle.textContent = view === "chat"
    ? (state.recipient ? `与 ${state.recipient} 的私聊` : `#${state.roomName} · 多人实时聊天`) : meta[2];
  $$('[data-view-panel]').forEach((panel) => {
    const active = panel.dataset.viewPanel === view;
    panel.hidden = !active;
    panel.classList.toggle("active", active);
  });
  $$('[data-view]').forEach((button) => button.classList.toggle("active", button.dataset.view === view));
  els.membersToggle.hidden = view !== "chat";
  els.quickHistory.hidden = view === "history";
  els.membersPanel.classList.remove("open");
  if (view === "dictionary" && state.clientId) {
    loadRecentLookups();
    setTimeout(() => els.dictionaryInput.focus(), 0);
  }
  if (view === "history" && state.clientId) {
    loadHistory();
    setTimeout(() => els.historyInput.focus(), 0);
  }
  if (view === "music") loadMusic();
  if (view === "game") loadGameScores();
}

const GAME_QUESTIONS = [
  ["ability", "能力"], ["network", "网络"], ["friend", "朋友"], ["dream", "梦想"],
  ["music", "音乐"], ["garden", "花园"], ["future", "未来"], ["memory", "记忆"],
  ["language", "语言"], ["computer", "计算机"], ["beautiful", "美丽的"], ["freedom", "自由"],
  ["knowledge", "知识"], ["challenge", "挑战"], ["victory", "胜利"], ["weather", "天气"],
  ["journey", "旅程"], ["mystery", "神秘"], ["courage", "勇气"], ["peace", "和平"],
];

const game = { running: false, paused: false, score: 0, combo: 0, lives: 3, time: 60, timer: null, order: [], cursor: 0 };

function shuffled(items) {
  const copy = [...items];
  for (let index = copy.length - 1; index > 0; index--) {
    const target = Math.floor(Math.random() * (index + 1));
    [copy[index], copy[target]] = [copy[target], copy[index]];
  }
  return copy;
}

function updateGameStats() {
  els.gameScore.textContent = String(game.score);
  els.gameCombo.textContent = `×${game.combo}`;
  els.gameLives.textContent = Array.from({ length: 3 }, (_, index) => index < game.lives ? "♥" : "♡").join(" ");
  els.gameTime.textContent = String(game.time);
}

function nextGameQuestion() {
  if (!game.running || game.paused) return;
  if (game.cursor >= game.order.length) { game.order = shuffled(GAME_QUESTIONS); game.cursor = 0; }
  const question = game.order[game.cursor++];
  const wrong = shuffled(GAME_QUESTIONS.filter((item) => item[0] !== question[0])).slice(0, 3).map((item) => item[1]);
  els.gameWord.textContent = question[0];
  els.gameHint.textContent = "请选择这个英文单词的正确中文释义";
  els.gameAnswers.replaceChildren();
  shuffled([question[1], ...wrong]).forEach((meaning) => {
    const button = document.createElement("button");
    button.type = "button";
    button.textContent = meaning;
    button.addEventListener("click", () => answerGame(button, meaning === question[1], question[1]));
    els.gameAnswers.append(button);
  });
  els.gameAnswers.hidden = false;
}

function answerGame(selected, correct, expected) {
  if (!game.running || game.paused) return;
  Array.from(els.gameAnswers.children).forEach((button) => {
    button.disabled = true;
    if (button.textContent === expected) button.classList.add("correct");
  });
  if (correct) {
    game.combo += 1;
    game.score += 100 + Math.min(game.combo, 10) * 20;
    selected.classList.add("correct");
    els.gameHint.textContent = `答对了！连击 ×${game.combo}`;
  } else {
    game.combo = 0;
    game.lives -= 1;
    selected.classList.add("wrong");
    els.gameHint.textContent = `正确答案：${expected}`;
  }
  updateGameStats();
  if (game.lives <= 0) setTimeout(endGame, 450);
  else setTimeout(nextGameQuestion, 450);
}

function startGame() {
  if (!state.clientId) return showToast("请先登录再开始游戏");
  clearInterval(game.timer);
  Object.assign(game, { running: true, paused: false, score: 0, combo: 0, lives: 3, time: 60, order: shuffled(GAME_QUESTIONS), cursor: 0 });
  els.gameStart.textContent = "重新开始";
  els.gamePause.disabled = false;
  els.gamePause.textContent = "暂停";
  updateGameStats();
  nextGameQuestion();
  game.timer = setInterval(() => {
    if (!game.running || game.paused) return;
    game.time -= 1;
    updateGameStats();
    if (game.time <= 0) endGame();
  }, 1000);
}

function toggleGamePause() {
  if (!game.running) return;
  game.paused = !game.paused;
  els.gamePause.textContent = game.paused ? "继续" : "暂停";
  Array.from(els.gameAnswers.children).forEach((button) => { button.disabled = game.paused; });
  els.gameHint.textContent = game.paused ? "游戏已暂停" : "请选择正确释义";
}

async function endGame() {
  if (!game.running) return;
  game.running = false;
  clearInterval(game.timer);
  els.gameAnswers.hidden = true;
  els.gameWord.textContent = "挑战结束";
  els.gameHint.textContent = `最终得分 ${game.score}，成绩已提交在线排行榜。`;
  els.gameStart.textContent = "再玩一次";
  els.gamePause.disabled = true;
  try {
    await api("/api/game/scores", { method: "POST", body: JSON.stringify({ clientId: state.clientId, score: game.score }) });
    await loadGameScores();
  } catch (error) { showToast(error.message); }
}

function renderGameScores(scores) {
  els.scoreList.replaceChildren();
  if (!scores.length) {
    const empty = document.createElement("p");
    empty.className = "empty-note";
    empty.textContent = "还没有成绩，来成为第一名吧。";
    els.scoreList.append(empty);
    return;
  }
  scores.forEach((entry) => {
    const row = document.createElement("article");
    row.className = `score-item${entry.rank <= 3 ? " podium" : ""}`;
    const rank = document.createElement("b"); rank.textContent = String(entry.rank).padStart(2, "0");
    const copy = document.createElement("span");
    const name = document.createElement("strong"); name.textContent = entry.name;
    const time = document.createElement("small"); time.textContent = entry.time;
    copy.append(name, time);
    const score = document.createElement("em"); score.textContent = String(entry.score);
    row.append(rank, copy, score);
    els.scoreList.append(row);
  });
}

async function loadGameScores() {
  try { renderGameScores((await api("/api/game/scores")).scores); }
  catch (error) { showToast(error.message); }
}

function formatDuration(seconds) {
  if (!Number.isFinite(seconds)) return "0:00";
  const minutes = Math.floor(seconds / 60);
  return `${minutes}:${String(Math.floor(seconds % 60)).padStart(2, "0")}`;
}

function formatBytes(bytes) {
  return bytes >= 1024 * 1024 ? `${(bytes / 1024 / 1024).toFixed(1)} MB` : `${Math.ceil(bytes / 1024)} KB`;
}

function selectTrack(name) {
  state.currentTrack = name;
  els.currentTrackName.textContent = name.replace(/\.[^.]+$/, "");
  els.playerStatus.textContent = "已选择，正在缓冲音乐……";
  els.audioPlayer.src = `/api/music/file?name=${encodeURIComponent(name)}`;
  els.audioPlayer.load();
  els.playPauseButton.disabled = false;
  els.audioPlayer.play().catch(() => {
    els.playerStatus.textContent = "音乐已就绪，点击开始播放。";
  });
  $$(".music-item").forEach((item) => item.classList.toggle("active", item.dataset.name === name));
}

function renderMusic(tracks) {
  els.musicList.replaceChildren();
  if (!tracks.length) {
    const empty = document.createElement("p");
    empty.className = "empty-note";
    empty.textContent = "曲库还是空的，请由管理员上传音乐。";
    els.musicList.append(empty);
    return;
  }
  tracks.forEach((track, index) => {
    const row = document.createElement("article");
    row.className = `music-item${track.name === state.currentTrack ? " active" : ""}`;
    row.dataset.name = track.name;
    const number = document.createElement("span");
    number.className = "track-number";
    number.textContent = String(index + 1).padStart(2, "0");
    const copy = document.createElement("button");
    copy.type = "button";
    copy.className = "track-select";
    const title = document.createElement("strong");
    title.textContent = track.name.replace(/\.[^.]+$/, "");
    const detail = document.createElement("small");
    detail.textContent = `${track.name.split(".").pop().toUpperCase()} · ${formatBytes(track.size)}`;
    copy.append(title, detail);
    copy.addEventListener("click", () => selectTrack(track.name));
    const remove = document.createElement("button");
    remove.type = "button";
    remove.className = "track-delete";
    remove.textContent = "删除";
    remove.title = `删除 ${track.name}`;
    remove.addEventListener("click", () => deleteMusic(track.name));
    row.append(number, copy, remove);
    els.musicList.append(row);
  });
}

async function loadMusic() {
  try {
    const data = await api("/api/music");
    renderMusic(data.tracks);
  } catch (error) {
    showToast(error.message);
  }
}

async function adminMusicRequest(path, options) {
  const response = await fetch(path, options);
  const data = await response.json().catch(() => ({}));
  if (!response.ok) throw new Error(data.error || `操作失败 (${response.status})`);
  return data;
}

async function uploadMusic() {
  const file = els.musicFile.files[0];
  const password = els.adminPassword.value;
  if (!password) return showToast("请先输入管理员口令");
  if (!file) return showToast("请选择音乐文件");
  if (file.size > 25 * 1024 * 1024) return showToast("音乐文件不能超过 25 MB");
  els.uploadMusic.disabled = true;
  els.uploadMusic.textContent = "正在上传……";
  try {
    await adminMusicRequest(`/api/admin/music?name=${encodeURIComponent(file.name)}`, {
      method: "POST",
      headers: { "Content-Type": "application/octet-stream", "X-Admin-Password": password },
      body: file,
    });
    els.musicFile.value = "";
    showToast("音乐已上传");
    await loadMusic();
  } catch (error) {
    showToast(error.message);
  } finally {
    els.uploadMusic.disabled = false;
    els.uploadMusic.textContent = "上传到曲库";
  }
}

async function deleteMusic(name) {
  const password = els.adminPassword.value;
  if (!password) return showToast("请在管理员接口中输入口令");
  if (!window.confirm(`确定删除“${name}”吗？`)) return;
  try {
    await adminMusicRequest(`/api/admin/music?name=${encodeURIComponent(name)}`, {
      method: "DELETE",
      headers: { "X-Admin-Password": password },
    });
    if (state.currentTrack === name) {
      els.audioPlayer.pause();
      els.audioPlayer.removeAttribute("src");
      state.currentTrack = "";
      els.currentTrackName.textContent = "还没有选择音乐";
      els.playerStatus.textContent = "从曲库中选择一首音乐开始播放。";
      els.playPauseButton.disabled = true;
    }
    showToast("音乐已删除");
    await loadMusic();
  } catch (error) {
    showToast(error.message);
  }
}

function renderMessage(message) {
  if (!state.renderedDivider) {
    const divider = document.createElement("div");
    divider.className = "day-divider";
    divider.textContent = "今天";
    els.messageList.append(divider);
    state.renderedDivider = true;
  }
  if (message.kind === "system") {
    const line = document.createElement("p");
    line.className = "system-message";
    line.textContent = `— ${message.text} · ${message.time} —`;
    els.messageList.append(line);
    return;
  }

  const article = document.createElement("article");
  const isBot = message.kind === "bot";
  const isMine = message.kind === "user" && message.name === state.name;
  article.className = `message${isBot ? " bot-message" : ""}${isMine ? " mine" : ""}`;
  article.append(createAvatar(message.name, isBot));
  const content = document.createElement("div");
  content.className = "message-content";
  const head = document.createElement("div");
  head.className = "message-head";
  const author = document.createElement("strong");
  author.textContent = isMine ? `${message.name}（我）` : message.name;
  const time = document.createElement("time");
  time.textContent = message.time;
  head.append(author, time);
  const body = document.createElement("div");
  body.className = "message-body";
  body.textContent = message.text;
  content.append(head, body);
  article.append(content);
  els.messageList.append(article);
}

function renderUsers(users) {
  els.memberList.replaceChildren();
  users.forEach((user) => {
    const row = document.createElement("div");
    row.className = "member";
    row.append(createAvatar(user.name, user.bot));
    const copy = document.createElement("span");
    const name = document.createElement("strong");
    name.textContent = user.name === state.name ? `${user.name}（我）` : user.name;
    const status = document.createElement("small");
    status.textContent = user.bot ? "随时可以查词" : "在线";
    copy.append(name, status);
    row.append(copy);
    if (!user.bot && user.name !== state.name) {
      row.classList.add("clickable");
      row.title = `私聊 ${user.name}`;
      row.addEventListener("click", () => switchChannel({ recipient: user.name }));
    }
    els.memberList.append(row);
  });
  els.memberCount.textContent = String(users.length);
  els.roomCount.textContent = String(users.length);
}

function channelButton(icon, label, unread, active, onClick) {
  const button = document.createElement("button");
  button.type = "button";
  button.className = `channel-button${active ? " active" : ""}`;
  const mark = document.createElement("i"); mark.textContent = icon;
  const text = document.createElement("span"); text.textContent = label;
  const badge = document.createElement("b"); badge.textContent = unread > 0 ? String(unread) : "";
  button.append(mark, text, badge);
  button.addEventListener("click", onClick);
  return button;
}

function renderChannels(rooms, contacts) {
  els.roomList.replaceChildren();
  rooms.forEach((room) => {
    const active = !state.recipient && state.roomId === room.id;
    els.roomList.append(channelButton("#", room.name, room.unread, active,
      () => switchChannel({ roomId: room.id, roomName: room.name })));
  });
  els.contactList.replaceChildren();
  if (!contacts.length) {
    const empty = document.createElement("p"); empty.className = "channel-empty"; empty.textContent = "暂无其他用户";
    els.contactList.append(empty);
  } else {
    contacts.forEach((contact) => {
      const active = state.recipient.toLowerCase() === contact.name.toLowerCase();
      els.contactList.append(channelButton("@", contact.name, contact.unread, active,
        () => switchChannel({ recipient: contact.name })));
    });
  }
}

async function loadChannels() {
  if (!state.clientId || state.loadingChannels) return;
  state.loadingChannels = true;
  try {
    const params = new URLSearchParams({ clientId: state.clientId });
    const [roomData, contactData] = await Promise.all([
      api(`/api/rooms?${params}`), api(`/api/contacts?${params}`),
    ]);
    const signature = JSON.stringify([roomData.rooms, contactData.contacts]);
    if (signature !== state.channelSignature) {
      state.channelSignature = signature;
      renderChannels(roomData.rooms, contactData.contacts);
    }
  } catch (error) {
    if (!String(error.message).includes("登录已失效")) showToast(error.message);
  } finally { state.loadingChannels = false; }
}

async function switchChannel({ roomId = "direct", roomName = "", recipient = "" }) {
  state.roomId = roomId;
  state.roomName = roomName || state.roomName;
  state.recipient = recipient;
  state.lastId = 0;
  state.renderedDivider = false;
  state.channelSignature = "";
  els.messageList.replaceChildren();
  els.messageInput.placeholder = recipient ? `私聊 ${recipient}` : `发消息到 #${state.roomName}`;
  setView("chat");
  await poll();
  await loadChannels();
  els.messageInput.focus();
}

function setConnection(connected) {
  els.banner.hidden = connected;
  els.serverDetail.textContent = connected ? "WinSock · 多线程 · HTTP/JSON" : "正在重新连接";
}

async function poll() {
  if (!state.clientId || state.polling) return;
  state.polling = true;
  try {
    const query = new URLSearchParams({ clientId: state.clientId, after: String(state.lastId), roomId: state.roomId });
    if (state.recipient) query.set("recipient", state.recipient);
    const data = await api(`/api/messages?${query}`);
    const nearBottom = els.messageList.scrollHeight - els.messageList.scrollTop - els.messageList.clientHeight < 120;
    data.messages.forEach(renderMessage);
    state.lastId = data.lastId;
    renderUsers(data.users);
    if (nearBottom || data.messages.some((item) => item.name === state.name)) {
      els.messageList.scrollTop = els.messageList.scrollHeight;
    }
    setConnection(true);
    state.channelTick += 1;
    if (data.messages.length || state.channelTick % 4 === 0) loadChannels();
  } catch (error) {
    setConnection(false);
    if (String(error.message).includes("登录已失效")) resetSession();
  } finally {
    state.polling = false;
  }
}

function resetSession() {
  state.clientId = "";
  state.name = "";
  state.lastId = 0;
  state.renderedDivider = false;
  state.roomId = "lobby";
  state.roomName = "公共大厅";
  state.recipient = "";
  state.accountMode = "guest";
  state.channelSignature = "";
  els.messageList.replaceChildren();
  els.roomList.replaceChildren();
  els.contactList.replaceChildren();
  els.profileName.textContent = "尚未登录";
  els.profileAvatar.textContent = "?";
  els.loginModal.classList.remove("hidden");
  els.passwordInput.value = "";
  setAuthMode("guest");
  els.nameInput.focus();
}

async function lookupWord(word) {
  const query = word.trim();
  if (!query) return;
  els.dictionaryResult.classList.add("loading");
  try {
    const params = new URLSearchParams({ clientId: state.clientId, q: query });
    const data = await api(`/api/dictionary?${params}`);
    const lines = data.result.split("\n");
    els.resultWord.textContent = data.query;
    els.resultMeaning.textContent = lines[0].toLowerCase() === data.query.toLowerCase() ? lines.slice(1).join("\n") : data.result;
    els.resultTime.textContent = `查询于 ${data.time}`;
    els.dictionaryResult.classList.remove("empty");
    els.dictionaryInput.value = data.query;
    await loadRecentLookups();
  } catch (error) {
    showToast(error.message);
  } finally {
    els.dictionaryResult.classList.remove("loading");
  }
}

function renderRecentLookups(items) {
  els.recentLookupList.replaceChildren();
  if (!items.length) {
    const empty = document.createElement("p");
    empty.className = "empty-note";
    empty.textContent = "还没有查词记录";
    els.recentLookupList.append(empty);
    return;
  }
  items.slice(0, 5).forEach((item) => {
    const row = document.createElement("button");
    row.type = "button";
    row.className = "recent-item";
    const mark = document.createElement("span");
    mark.className = "recent-mark";
    mark.textContent = "译";
    const copy = document.createElement("span");
    const title = document.createElement("strong");
    title.textContent = item.query;
    const meaning = document.createElement("small");
    meaning.textContent = item.result.replace(/\n/g, " ");
    copy.append(title, meaning);
    const time = document.createElement("time");
    time.textContent = item.time;
    row.append(mark, copy, time);
    row.addEventListener("click", () => lookupWord(item.query));
    els.recentLookupList.append(row);
  });
}

async function loadRecentLookups() {
  try {
    const params = new URLSearchParams({ clientId: state.clientId, type: "dictionary", q: "" });
    const data = await api(`/api/history?${params}`);
    renderRecentLookups(data.items);
  } catch (error) {
    showToast(error.message);
  }
}

function renderHistory(items) {
  els.historyList.replaceChildren();
  els.historyCount.textContent = `${items.length} 条记录`;
  if (!items.length) {
    const empty = document.createElement("p");
    empty.className = "empty-note";
    empty.textContent = "没有找到匹配记录";
    els.historyList.append(empty);
    return;
  }
  items.forEach((item) => {
    const row = document.createElement("article");
    row.className = "history-item";
    const mark = document.createElement("span");
    mark.className = "history-mark";
    mark.textContent = state.historyType === "dictionary" ? "译" : item.kind === "bot" ? "机" : item.kind === "system" ? "系" : "聊";
    const copy = document.createElement("div");
    const title = document.createElement("strong");
    title.textContent = state.historyType === "dictionary" ? item.query : item.name;
    const text = document.createElement("p");
    text.textContent = state.historyType === "dictionary" ? item.result : item.text;
    copy.append(title, text);
    const time = document.createElement("time");
    time.textContent = item.time;
    row.append(mark, copy, time);
    els.historyList.append(row);
  });
}

async function loadHistory() {
  try {
    const params = new URLSearchParams({
      clientId: state.clientId,
      type: state.historyType,
      q: els.historyInput.value.trim(),
    });
    const data = await api(`/api/history?${params}`);
    renderHistory(data.items);
  } catch (error) {
    showToast(error.message);
  }
}

function setAuthMode(mode) {
  state.authMode = mode;
  $$('[data-auth-mode]').forEach((button) => button.classList.toggle("active", button.dataset.authMode === mode));
  const guest = mode === "guest";
  els.passwordGroup.hidden = guest;
  els.passwordInput.required = !guest;
  els.nameLabel.textContent = guest ? "你的昵称" : "用户名";
  els.nameInput.placeholder = guest ? "例如：小林" : "输入注册用户名";
  els.nameInput.autocomplete = guest ? "nickname" : "username";
  els.passwordInput.autocomplete = mode === "register" ? "new-password" : "current-password";
  els.authDescription.textContent = guest
    ? "游客无需注册；注册账户可保存未读消息并创建聊天室。"
    : mode === "login" ? "使用注册账户登录，继续查看持久化消息和未读状态。"
      : "创建持久账户；密码只保存 PBKDF2-SHA256 哈希。";
  els.authSubmit.firstChild.textContent = mode === "register" ? "注册并进入 " : mode === "login" ? "登录 " : "游客进入 ";
  els.loginError.textContent = "";
}

$$('[data-auth-mode]').forEach((button) => button.addEventListener("click", () => setAuthMode(button.dataset.authMode)));

els.loginForm.addEventListener("submit", async (event) => {
  event.preventDefault();
  els.loginError.textContent = "";
  const submit = event.submitter;
  submit.disabled = true;
  try {
    const endpoint = state.authMode === "guest" ? "/api/join" : state.authMode === "login" ? "/api/login" : "/api/register";
    const body = { name: els.nameInput.value };
    if (state.authMode !== "guest") body.password = els.passwordInput.value;
    const data = await api(endpoint, { method: "POST", body: JSON.stringify(body) });
    state.clientId = data.clientId;
    state.name = data.name;
    state.accountMode = data.authMode || state.authMode;
    els.profileName.textContent = data.name;
    els.profileAvatar.textContent = initials(data.name);
    els.loginModal.classList.add("hidden");
    setView("chat");
    els.messageInput.focus();
    await loadChannels();
    await poll();
  } catch (error) {
    els.loginError.textContent = error.message;
  } finally {
    submit.disabled = false;
  }
});

els.messageForm.addEventListener("submit", async (event) => {
  event.preventDefault();
  const text = els.messageInput.value.trim();
  if (!text || !state.clientId) return;
  const button = els.messageForm.querySelector(".send-button");
  button.disabled = true;
  try {
    await api("/api/messages", { method: "POST", body: JSON.stringify({
      clientId: state.clientId, text, roomId: state.roomId, recipient: state.recipient,
    }) });
    els.messageInput.value = "";
    resizeMessageInput();
    await poll();
  } catch (error) {
    showToast(error.message);
  } finally {
    button.disabled = false;
    els.messageInput.focus();
  }
});

function resizeMessageInput() {
  els.messageInput.style.height = "auto";
  els.messageInput.style.height = `${Math.min(els.messageInput.scrollHeight, 100)}px`;
  els.messageCounter.textContent = `${els.messageInput.value.length} / 500`;
}

async function insertBotCommand() {
  if (state.recipient) await switchChannel({ roomId: "lobby", roomName: "公共大厅" });
  else setView("chat");
  els.messageInput.value = "@词典 ";
  resizeMessageInput();
  els.messageInput.focus();
  els.messageInput.setSelectionRange(4, 4);
}

els.messageInput.addEventListener("input", resizeMessageInput);
els.messageInput.addEventListener("keydown", (event) => {
  if (event.key === "Enter" && !event.shiftKey) {
    event.preventDefault();
    els.messageForm.requestSubmit();
  }
});
els.commandButton.addEventListener("click", insertBotCommand);
els.botShortcut.addEventListener("click", insertBotCommand);

els.addRoomButton.addEventListener("click", async () => {
  if (state.accountMode !== "account") return showToast("注册用户才能创建聊天室");
  const name = window.prompt("输入新聊天室名称（2～20 个字符）");
  if (!name) return;
  try {
    const room = await api("/api/rooms", { method: "POST", body: JSON.stringify({ clientId: state.clientId, name }) });
    await loadChannels();
    await switchChannel({ roomId: room.id, roomName: room.name });
  } catch (error) { showToast(error.message); }
});

els.dictionaryForm.addEventListener("submit", (event) => {
  event.preventDefault();
  lookupWord(els.dictionaryInput.value);
});
$$('[data-word]').forEach((button) => button.addEventListener("click", () => lookupWord(button.dataset.word)));

els.historyForm.addEventListener("submit", (event) => {
  event.preventDefault();
  loadHistory();
});
$$('[data-history-type]').forEach((button) => button.addEventListener("click", () => {
  state.historyType = button.dataset.historyType;
  $$('[data-history-type]').forEach((tab) => tab.classList.toggle("active", tab === button));
  els.historyInput.placeholder = state.historyType === "chat" ? "搜索昵称或消息内容" : "搜索单词或释义";
  loadHistory();
}));
els.clearHistorySearch.addEventListener("click", () => {
  els.historyInput.value = "";
  loadHistory();
});

els.playPauseButton.addEventListener("click", () => {
  if (!state.currentTrack) return;
  if (els.audioPlayer.paused) els.audioPlayer.play().catch(() => showToast("无法播放这个音频文件"));
  else els.audioPlayer.pause();
});
els.audioPlayer.addEventListener("play", () => {
  els.playPauseButton.textContent = "❚❚ 暂停";
  els.playerStatus.textContent = "正在播放";
  document.querySelector(".record-disc").classList.add("spinning");
});
els.audioPlayer.addEventListener("pause", () => {
  els.playPauseButton.textContent = "▶ 开始";
  if (state.currentTrack) els.playerStatus.textContent = "已暂停";
  document.querySelector(".record-disc").classList.remove("spinning");
});
els.audioPlayer.addEventListener("loadedmetadata", () => {
  els.durationTime.textContent = formatDuration(els.audioPlayer.duration);
});
els.audioPlayer.addEventListener("timeupdate", () => {
  els.currentTime.textContent = formatDuration(els.audioPlayer.currentTime);
  els.musicProgress.value = els.audioPlayer.duration
    ? String((els.audioPlayer.currentTime / els.audioPlayer.duration) * 100) : "0";
});
els.audioPlayer.addEventListener("ended", () => { els.playerStatus.textContent = "播放结束"; });
els.musicProgress.addEventListener("input", () => {
  if (els.audioPlayer.duration) els.audioPlayer.currentTime = els.audioPlayer.duration * Number(els.musicProgress.value) / 100;
});
els.volumeControl.addEventListener("input", () => { els.audioPlayer.volume = Number(els.volumeControl.value); });
els.audioPlayer.volume = Number(els.volumeControl.value);
els.refreshMusic.addEventListener("click", loadMusic);
els.uploadMusic.addEventListener("click", uploadMusic);
els.gameStart.addEventListener("click", startGame);
els.gamePause.addEventListener("click", toggleGamePause);
els.refreshScores.addEventListener("click", loadGameScores);

$$('[data-view]').forEach((button) => button.addEventListener("click", () => setView(button.dataset.view)));
els.quickHistory.addEventListener("click", () => setView("history"));
els.membersToggle.addEventListener("click", () => els.membersPanel.classList.toggle("open"));

els.themeButton.addEventListener("click", () => toggleThemeDrawer(true));
els.sceneShortcut.addEventListener("click", () => toggleThemeDrawer(true));
els.closeThemeDrawer.addEventListener("click", () => toggleThemeDrawer(false));
els.transparencyToggle.addEventListener("change", () => setTransparency(els.transparencyToggle.checked));
els.fireworksToggle.addEventListener("change", () => setFireworks(els.fireworksToggle.checked));
$$('[data-scene]').forEach((button) => button.addEventListener("click", () => {
  setScene(button.dataset.scene);
  toggleThemeDrawer(false);
}));

els.leaveButton.addEventListener("click", () => {
  if (state.clientId) {
    navigator.sendBeacon("/api/leave", new Blob([JSON.stringify({ clientId: state.clientId })], { type: "application/json" }));
  }
  resetSession();
});
window.addEventListener("beforeunload", () => {
  if (state.clientId) navigator.sendBeacon("/api/leave", new Blob([JSON.stringify({ clientId: state.clientId })], { type: "application/json" }));
});

setAuthMode("guest");
setScene(localStorage.getItem("gensokyo-scene") || "01");
setTransparency(localStorage.getItem("gensokyo-transparency") === "1");
setFireworks(localStorage.getItem("gensokyo-fireworks") === "1");
window.addEventListener("resize", () => { if (fireworks.enabled) resizeFireworks(); });
setView("chat");
api("/api/health").then((health) => {
  els.serverDetail.textContent = `WinSock · ${health.dictionarySize} 个词条`;
}).catch(() => setConnection(false));
setInterval(poll, 1200);
