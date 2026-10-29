const $ = id => document.getElementById(id);
const BOARD_IDS = [1, 2, 3];
const COLORS = {1: '#2864d7', 2: '#d97316', 3: '#16845b'};
const BOARD_NAMES = {1: '板 1', 2: '板 2', 3: '板 3'};
const PAGE_SIZE = 40;
const state = {
  source: 'live',
  board: '',
  liveSamples: [],
  pageSamples: [],
  page: 1,
  pageCount: 0,
  total: 0,
  latest: new Map(),
  boards: [],
  refreshing: false,
};

function number(value, digits = 2) {
  return value === null || value === undefined || Number.isNaN(Number(value)) ? '--' : Number(value).toFixed(digits);
}

function dateTime(value) {
  if (!value) return '--';
  const date = new Date(value);
  if (Number.isNaN(date.getTime())) return '--';
  const parts = new Intl.DateTimeFormat('zh-CN', {
    year: 'numeric', month: '2-digit', day: '2-digit', hour: '2-digit',
    minute: '2-digit', second: '2-digit', hour12: false,
  }).formatToParts(date).reduce((result, part) => ({...result, [part.type]: part.value}), {});
  return `${parts.year}-${parts.month}-${parts.day} ${parts.hour}:${parts.minute}:${parts.second}.${String(date.getMilliseconds()).padStart(3, '0')}`;
}

function clock(value) {
  const date = new Date(value);
  return Number.isNaN(date.getTime()) ? '' : date.toLocaleTimeString('zh-CN', {hour12: false});
}

function visibleSamples() {
  return state.source === 'live' ? state.liveSamples : state.pageSamples;
}

function selectedSamples() {
  return visibleSamples().filter(sample => !state.board || sample.board_id === Number(state.board));
}

function resizeCanvas(canvas) {
  const width = Math.max(1, canvas.clientWidth);
  const height = Math.max(1, canvas.clientHeight);
  const ratio = window.devicePixelRatio || 1;
  if (canvas.width !== Math.round(width * ratio) || canvas.height !== Math.round(height * ratio)) {
    canvas.width = Math.round(width * ratio);
    canvas.height = Math.round(height * ratio);
  }
  const context = canvas.getContext('2d');
  context.setTransform(ratio, 0, 0, ratio, 0, 0);
  return {context, width, height};
}

function prepareChart(canvas, metric, fixedRange) {
  const {context, width, height} = resizeCanvas(canvas);
  context.clearRect(0, 0, width, height);
  const samples = selectedSamples().filter(sample => sample.board_id >= 1 && sample.board_id <= 3);
  const left = 43, right = 10, top = 12, bottom = 27;
  const plotWidth = Math.max(1, width - left - right);
  const plotHeight = Math.max(1, height - top - bottom);
  const values = samples.map(sample => sample[metric]).filter(value => value !== null && value !== undefined).map(Number);
  let minimum = fixedRange ? fixedRange[0] : Math.min(...values);
  let maximum = fixedRange ? fixedRange[1] : Math.max(...values);
  if (!values.length && !fixedRange) { minimum = 0; maximum = 1; }
  if (!fixedRange) {
    const span = Math.max(1, maximum - minimum);
    minimum -= span * 0.12;
    maximum += span * 0.12;
  }
  const times = samples.map(sample => new Date(sample.timestamp).getTime()).filter(Number.isFinite);
  const start = times.length ? Math.min(...times) : Date.now() - 60000;
  const end = times.length ? Math.max(start + 2000, ...times) : Date.now();
  context.font = '11px "Microsoft YaHei", system-ui, sans-serif';
  context.lineWidth = 1;
  for (let index = 0; index < 5; index++) {
    const y = top + plotHeight * index / 4;
    context.strokeStyle = '#edf1f6';
    context.beginPath(); context.moveTo(left, y); context.lineTo(left + plotWidth, y); context.stroke();
    context.fillStyle = '#7c899a'; context.textAlign = 'right';
    context.fillText((maximum - (maximum - minimum) * index / 4).toFixed(2), left - 7, y + 4);
  }
  context.fillStyle = '#7c899a';
  context.textAlign = 'left'; context.fillText(clock(start), left, height - 8);
  context.textAlign = 'right'; context.fillText(clock(end), left + plotWidth, height - 8);
  return {context, samples, left, top, plotWidth, plotHeight, start: Math.min(start, end - 1), end, minimum, maximum};
}

function pointOf(frame, sample, metric) {
  const time = new Date(sample.timestamp).getTime();
  return {
    x: frame.left + (time - frame.start) / (frame.end - frame.start) * frame.plotWidth,
    y: frame.top + (frame.maximum - Number(sample[metric])) / (frame.maximum - frame.minimum) * frame.plotHeight,
    time,
  };
}

function boardSamples(frame, board, metric) {
  return frame.samples.filter(sample => sample.board_id === board && sample[metric] !== null && sample[metric] !== undefined)
    .sort((a, b) => new Date(a.timestamp) - new Date(b.timestamp));
}

function drawEmpty(frame, message = '等待有效采集数据') {
  frame.context.fillStyle = '#8491a3';
  frame.context.textAlign = 'center';
  frame.context.fillText(message, frame.left + frame.plotWidth / 2, frame.top + frame.plotHeight / 2);
}

function drawLegend(frame, y = frame.top + 5) {
  let x = frame.left + 8;
  frame.context.font = '11px "Microsoft YaHei", system-ui, sans-serif';
  frame.context.textAlign = 'left';
  for (const board of BOARD_IDS) {
    frame.context.fillStyle = COLORS[board];
    frame.context.fillRect(x, y, 15, 2);
    frame.context.fillText(BOARD_NAMES[board], x + 19, y + 4);
    x += 62;
  }
}

function drawTemperature() {
  const metric = 'temperature';
  const frame = prepareChart($('temperature-chart'), metric);
  let drawn = false;
  for (const board of BOARD_IDS) {
    const points = boardSamples(frame, board, metric).map(sample => pointOf(frame, sample, metric));
    if (!points.length) continue;
    drawn = true;
    frame.context.strokeStyle = COLORS[board]; frame.context.lineWidth = 2; frame.context.beginPath();
    points.forEach((point, index) => index ? frame.context.lineTo(point.x, point.y) : frame.context.moveTo(point.x, point.y));
    frame.context.stroke();
    frame.context.fillStyle = COLORS[board];
    for (const point of points) { frame.context.beginPath(); frame.context.arc(point.x, point.y, 2.2, 0, Math.PI * 2); frame.context.fill(); }
  }
  if (!drawn) drawEmpty(frame); else drawLegend(frame);
}

function drawHumidity() {
  const metric = 'humidity';
  const frame = prepareChart($('humidity-chart'), metric, [0, 100]);
  let drawn = false;
  for (const board of BOARD_IDS) {
    const samples = boardSamples(frame, board, metric);
    if (!samples.length) continue;
    drawn = true;
    const points = samples.map(sample => pointOf(frame, sample, metric));
    const baseline = frame.top + frame.plotHeight;
    const gradient = frame.context.createLinearGradient(0, frame.top, 0, baseline);
    gradient.addColorStop(0, COLORS[board] + '73'); gradient.addColorStop(1, COLORS[board] + '08');
    frame.context.fillStyle = gradient; frame.context.beginPath();
    frame.context.moveTo(points[0].x, baseline); points.forEach(point => frame.context.lineTo(point.x, point.y));
    frame.context.lineTo(points.at(-1).x, baseline); frame.context.closePath(); frame.context.fill();
    frame.context.strokeStyle = COLORS[board]; frame.context.lineWidth = 1.6; frame.context.beginPath();
    points.forEach((point, index) => index ? frame.context.lineTo(point.x, point.y) : frame.context.moveTo(point.x, point.y)); frame.context.stroke();
  }
  if (!drawn) drawEmpty(frame); else drawLegend(frame);
}

function drawLight() {
  const metric = 'light_percent';
  const frame = prepareChart($('light-chart'), metric, [0, 100]);
  const timeSlots = [...new Set(frame.samples.map(sample => sample.timestamp))].slice(-18);
  if (!timeSlots.length) { drawEmpty(frame); return; }
  const slotWidth = frame.plotWidth / timeSlots.length;
  const barWidth = Math.max(2, Math.min(12, slotWidth / 4.2));
  for (const board of BOARD_IDS) {
    const samples = boardSamples(frame, board, metric);
    frame.context.fillStyle = COLORS[board];
    for (const sample of samples) {
      const slot = timeSlots.indexOf(sample.timestamp); if (slot < 0) continue;
      const point = pointOf(frame, sample, metric);
      const x = frame.left + slot * slotWidth + slotWidth / 2 + (board - 2) * (barWidth + 2);
      frame.context.fillRect(x - barWidth / 2, point.y, barWidth, frame.top + frame.plotHeight - point.y);
    }
  }
  drawLegend(frame);
}

function drawCo2() {
  const metric = 'co2_ppm';
  const frame = prepareChart($('co2-chart'), metric, [0, 5000]);
  let drawn = false;
  for (const board of BOARD_IDS) {
    const points = boardSamples(frame, board, metric).map(sample => pointOf(frame, sample, metric));
    if (!points.length) continue;
    drawn = true;
    frame.context.strokeStyle = COLORS[board] + 'aa'; frame.context.lineWidth = 1.2;
    frame.context.setLineDash([4, 4]); frame.context.beginPath();
    points.forEach((point, index) => {
      if (!index) { frame.context.moveTo(point.x, point.y); return; }
      frame.context.lineTo(point.x, points[index - 1].y); frame.context.lineTo(point.x, point.y);
    });
    frame.context.stroke(); frame.context.setLineDash([]);
    frame.context.fillStyle = COLORS[board];
    for (const point of points) { frame.context.beginPath(); frame.context.arc(point.x, point.y, 2.8, 0, Math.PI * 2); frame.context.fill(); }
  }
  if (!drawn) drawEmpty(frame); else drawLegend(frame);
}

function drawHcho() {
  const metric = 'formaldehyde_mg_m3';
  const frame = prepareChart($('hcho-chart'), metric, [0, 1]);
  let drawn = false;
  for (const board of BOARD_IDS) {
    const points = boardSamples(frame, board, metric).map(sample => pointOf(frame, sample, metric));
    if (!points.length) continue;
    drawn = true;
    frame.context.strokeStyle = COLORS[board] + '99'; frame.context.lineWidth = 1;
    frame.context.fillStyle = COLORS[board];
    for (const point of points) {
      const baseline = frame.top + frame.plotHeight;
      frame.context.beginPath(); frame.context.moveTo(point.x, baseline); frame.context.lineTo(point.x, point.y); frame.context.stroke();
      frame.context.beginPath(); frame.context.arc(point.x, point.y, 3.1, 0, Math.PI * 2); frame.context.fill();
      frame.context.beginPath(); frame.context.arc(point.x, point.y, 5.2, 0, Math.PI * 2); frame.context.stroke();
    }
  }
  if (!drawn) drawEmpty(frame); else drawLegend(frame);
}

function drawAllCharts() {
  drawTemperature();
  drawHumidity();
  drawLight();
  drawCo2();
  drawHcho();
}

function sensorStatus(valid) {
  const names = ['温湿度', '光敏', 'CO2', '甲醛'];
  if (valid === 15) return {text: '全部正常', error: false};
  const failed = names.filter((_, index) => !(valid & (1 << index)));
  return {text: failed.join(' / ') + ' 异常', error: true};
}

function renderTable() {
  const rows = $('rows');
  rows.replaceChildren();
  const filter = state.board ? Number(state.board) : null;
  let samples;
  if (state.source === 'live') {
    samples = state.liveSamples.filter(sample => !filter || sample.board_id === filter);
    state.total = samples.length;
    state.pageCount = Math.max(1, Math.ceil(samples.length / PAGE_SIZE));
    state.page = Math.max(1, Math.min(state.page, state.pageCount));
    const end = state.total - (state.page - 1) * PAGE_SIZE;
    const start = Math.max(0, end - PAGE_SIZE);
    samples = samples.slice(start, end);
  } else {
    samples = state.pageSamples.filter(sample => !filter || sample.board_id === filter);
  }
  for (const sample of samples.slice().reverse()) {
    const row = document.createElement('tr');
    const status = sensorStatus(sample.valid);
    const cells = [sample.number, BOARD_NAMES[sample.board_id], dateTime(sample.timestamp), dateTime(sample.received_at),
      number(sample.temperature), number(sample.humidity), sample.light_adc ?? '--', number(sample.light_percent),
      sample.co2_ppm ?? '--', number(sample.formaldehyde_mg_m3), sample.sharing ? '开启' : '关闭', status.text];
    cells.forEach((value, index) => {
      const cell = document.createElement('td');
      cell.textContent = value;
      if (index === 1) cell.className = `source${sample.board_id}`;
      if (index === 11 && status.error) cell.className = 'error';
      row.appendChild(cell);
    });
    rows.appendChild(row);
  }
  if (!samples.length) {
    const row = document.createElement('tr');
    const cell = document.createElement('td');
    cell.colSpan = 12;
    cell.textContent = state.source === 'history' ? 'MySQL 暂无历史记录' : '等待三块开发板发送数据……';
    row.appendChild(cell); rows.appendChild(row);
  }
  const safePage = state.total ? state.page : 0;
  const totalPages = state.total ? state.pageCount : 0;
  const offset = state.total ? (state.page - 1) * PAGE_SIZE : 0;
  $('sample-count').textContent = `${state.total} 条${state.source === 'live' ? '（内存实时）' : '（数据库）'}`;
  $('page-range').textContent = state.total
    ? `每页 ${PAGE_SIZE} 条 · 显示 ${offset + 1}–${Math.min(offset + PAGE_SIZE, state.total)} 条，共 ${state.total} 条`
    : `每页 ${PAGE_SIZE} 条 · 共 0 条`;
  $('page-status').textContent = `第 ${safePage} / ${totalPages} 页`;
  $('first-page').disabled = $('previous-page').disabled = state.page <= 1;
  $('next-page').disabled = $('last-page').disabled = state.page >= state.pageCount;
}

function updateBoardCards() {
  for (const board of BOARD_IDS) {
    const status = state.boards.find(item => item.board_id === board);
    const latest = state.latest.get(board);
    const prefix = `board${board}`;
    $(`${prefix}-state`).textContent = state.source === 'history'
      ? '历史数据'
      : status?.fresh ? '在线 · 采集中' : status?.connected ? '在线 · 等待数据' : '未连接';
    $(`${prefix}-temperature`).textContent = number(latest?.temperature);
    $(`${prefix}-humidity`).textContent = number(latest?.humidity);
    $(`${prefix}-light`).textContent = number(latest?.light_percent);
    $(`${prefix}-co2`).textContent = latest?.co2_ppm ?? '--';
    $(`${prefix}-hcho`).textContent = number(latest?.formaldehyde_mg_m3);
    $(`${prefix}-time`).textContent = latest ? `采集：${dateTime(latest.timestamp)}` : '暂无采集数据';
    const sharing = state.source === 'history' ? Boolean(latest?.sharing) : Boolean(status?.connected && status?.fresh && status?.sharing);
    if (board === 1) {
      $(`${prefix}-sharing`).textContent = `向板 2 / 板 3 共享：${sharing ? '已开启（KEY1）' : '已关闭（KEY2）'}`;
    } else {
      $(`${prefix}-sharing`).textContent = `OLED 与呼吸灯：${sharing ? '显示 / 呼吸' : '关闭'}`;
    }
  }
}

function renderMeta(payload) {
  $('developer').textContent = payload.developer_name;
  $('student').textContent = payload.student_id;
  state.boards = payload.boards;
  const status = payload.status;
  $('storage').textContent = status.database === 'memory'
    ? `内存演示 · 接收 ${status.received} 条 · 未写 MySQL`
    : `MySQL 已保存 ${status.saved} 条 · 待保存 ${status.pending} 条`;
  if (status.database_error) $('storage').textContent += ' · 数据库写入重试中';
  if (status.dropped) $('storage').textContent += ` · 缓冲丢弃 ${status.dropped} 条`;
  $('storage').className = status.database_error || status.dropped ? 'error' : '';
}

function render(payload) {
  renderMeta(payload);
  if (state.source === 'live') {
    state.liveSamples = payload.samples;
  } else {
    state.pageSamples = payload.samples;
    state.page = payload.page;
    state.pageCount = payload.page_count;
    state.total = payload.total;
  }
  state.latest = new Map();
  for (const sample of visibleSamples()) {
    if (!state.latest.has(sample.board_id) || new Date(sample.timestamp) >= new Date(state.latest.get(sample.board_id).timestamp)) {
      state.latest.set(sample.board_id, sample);
    }
  }
  updateBoardCards();
  renderTable();
  drawAllCharts();
  $('connection').textContent = `已刷新 ${new Date().toLocaleTimeString('zh-CN', {hour12: false})}`;
}

async function refresh() {
  if (state.refreshing) return;
  state.refreshing = true;
  const source = state.source;
  const params = new URLSearchParams({board: state.board});
  let path = '/api/data';
  if (source === 'history') {
    params.set('page', String(state.page));
    params.set('page_size', String(PAGE_SIZE));
    path = '/api/history';
  }
  try {
    const response = await fetch(`${path}?${params}`, {cache: 'no-store', signal: AbortSignal.timeout(6000)});
    const payload = await response.json();
    if (!response.ok) throw new Error(payload.error || '请求失败');
    if (source === state.source) render(payload);
  } catch (error) {
    $('connection').textContent = `连接异常：${error.message}`;
    $('connection').classList.add('error');
  } finally {
    state.refreshing = false;
  }
}

function changePage(page) {
  if (page < 1 || page > state.pageCount) return;
  state.page = page;
  if (state.source === 'history') refresh();
  else { renderTable(); drawAllCharts(); }
  document.querySelector('.table-scroll').scrollTop = 0;
}

$('first-page').addEventListener('click', () => changePage(1));
$('previous-page').addEventListener('click', () => changePage(state.page - 1));
$('next-page').addEventListener('click', () => changePage(state.page + 1));
$('last-page').addEventListener('click', () => changePage(state.pageCount));
$('board-filter').addEventListener('change', event => {
  state.board = event.target.value;
  state.page = 1;
  refresh();
});
$('source').addEventListener('change', event => {
  state.source = event.target.value;
  state.page = 1;
  refresh();
});
let resizeTimer = 0;
window.addEventListener('resize', () => {
  window.clearTimeout(resizeTimer);
  resizeTimer = window.setTimeout(drawAllCharts, 100);
});
refresh();
window.setInterval(() => { if (!document.hidden) refresh(); }, 2000);
