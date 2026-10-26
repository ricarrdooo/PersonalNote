(() => {
  'use strict';

  const STORAGE_KEY = 'shiye.prototype.v1';
  const SETTINGS_KEY = 'shiye.preferences.v1';
  const TODAY = '2026-10-21';
  const $ = (selector, root = document) => root.querySelector(selector);
  const $$ = (selector, root = document) => [...root.querySelectorAll(selector)];
  const escapeHTML = (value = '') => String(value).replace(/[&<>"']/g, char => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[char]));
  const notebookNames = { study: '学习与成长', work: '工作与项目', life: '生活与灵感' };
  const viewNames = { all: '全部笔记', today: '今天', inbox: '收集箱', favorites: '我的收藏', study: '学习与成长', work: '工作与项目', life: '生活与灵感', trash: '回收站' };
  const notebookIcons = { study: '📖', work: '💼', life: '🌿' };
  let notes = [];
  let selectedId = 'garden';
  let view = 'study';
  let sortAscending = false;
  let query = '';
  let saveTimer;
  let toastTimer;
  let savedRange;
  let slashRange;
  let composing = false;

  function currentStamp() {
    const time = new Intl.DateTimeFormat('en-GB', { timeZone: 'Asia/Shanghai', hour: '2-digit', minute: '2-digit', second: '2-digit', hour12: false }).format(new Date());
    return `${TODAY}T${time}+08:00`;
  }

  function seedNotes() {
    return [
      {
        id: 'garden', title: '建立自己的知识花园', notebook: 'study', tags: ['个人成长', '知识管理'], favorite: true, inbox: false, trashed: false, updatedAt: `${TODAY}T10:42:00+08:00`,
        html: '<p class="lead">笔记不只是信息的仓库，也可以是一座慢慢生长的花园。把零散的想法种下来，在回顾和连接中，让它们成为自己的知识。</p><h2>从记录，到连接</h2><p>最近开始尝试一种更轻盈的记录方式：不急着整理出完美的体系，先留下那些让我停下来思考的片刻。</p><blockquote class="callout"><span class="callout-icon" contenteditable="false">💡</span><div class="callout-content"><strong>让记录变得轻一点</strong><p>每一条笔记，只需要回答一个问题：它为什么对我有用？</p></div></blockquote><h2>给知识一点生长的空间</h2><p>好的笔记会随着我们一起变化。今天的疑问，可能就是明天一篇新笔记的起点。</p><div class="todo-list"><label><input type="checkbox" checked><span>建立一个随手记录的收集箱</span></label><label><input type="checkbox"><span>每周留 15 分钟，回顾最近的笔记</span></label><label><input type="checkbox"><span>用自己的话，写下一个新的理解</span></label></div><p class="closing-note">不必一次建成整座花园。今天，先种下一颗种子。🌱</p>'
      },
      {
        id: 'qt-study', title: 'Qt 学习：让界面有呼吸感', notebook: 'study', tags: ['Qt', '界面设计'], favorite: false, inbox: false, trashed: false, updatedAt: `${TODAY}T09:28:00+08:00`,
        html: '<p class="lead">界面的秩序感，往往藏在间距、层级和留白里。</p><h2>今天的三个发现</h2><ul><li>用统一的间距，让界面有自然的节奏。</li><li>让正文、说明和操作拥有不同的视觉层级。</li><li>交互反馈要及时，动效要克制。</li></ul><h2>一个简单的布局示例</h2><pre><code>ColumnLayout {\n    spacing: 16\n    Text { text: "今天，记录一点新东西" }\n    TextArea { placeholderText: "从一个想法开始…" }\n}</code></pre><blockquote>下一步：给常用控件建立统一的主题变量。</blockquote>'
      },
      {
        id: 'reading', title: '读书笔记｜让阅读真正发生', notebook: 'study', tags: ['阅读', '思考'], favorite: true, inbox: false, trashed: false, updatedAt: '2026-10-20T20:18:00+08:00',
        html: '<p class="lead">读完一本书以后，留下的未必是原句，而是看问题的新角度。</p><h2>我想记住的</h2><p>不要把阅读变成收集金句。遇到有触动的地方，停下来写一句自己的理解。</p><h2>把书里的想法带进生活</h2><div class="todo-list"><label><input type="checkbox"><span>选择一个观点，试着用一周时间实践</span></label><label><input type="checkbox"><span>向一个朋友解释这本书最有价值的想法</span></label></div><blockquote>真正属于自己的知识，是能够用自己的话说清楚的。</blockquote>'
      },
      {
        id: 'product', title: '拾页 · 产品设计备忘', notebook: 'work', tags: ['产品', '灵感'], favorite: true, inbox: false, trashed: false, updatedAt: `${TODAY}T08:56:00+08:00`,
        html: '<p class="lead">做一款愿意每天打开的笔记工具。</p><h2>从一个明确的问题开始</h2><p>当一个想法出现时，如何让记录的成本足够低？</p><ul><li>随时可用的收集箱。</li><li>打开后就能继续上次的思考。</li><li>让记录、整理和回顾自然衔接。</li></ul><h2>第一版的重点</h2><div class="todo-list"><label><input type="checkbox" checked><span>确定三栏结构与视觉方向</span></label><label><input type="checkbox"><span>验证块编辑和自动保存体验</span></label><label><input type="checkbox"><span>完成一键日记与常用模板</span></label></div>'
      },
      {
        id: 'weekly', title: '每周复盘｜留一点时间给自己', notebook: 'life', tags: ['复盘', '生活'], favorite: false, inbox: false, trashed: false, updatedAt: '2026-10-19T21:15:00+08:00',
        html: '<p class="lead">给忙碌的一周留一个温柔的句号。</p><h2>这周值得记住的事</h2><p>完成了一个一直拖延的小项目，也在下班路上发现了一家很安静的书店。</p><h2>做得不错的地方</h2><ul><li>每天留出一段没有通知打扰的专注时间。</li><li>把零散的灵感先记下来，少了一点遗忘的焦虑。</li></ul><h2>下周，想试试</h2><p>让周末慢一点，给散步、阅读和朋友多留一点时间。</p>'
      },
      {
        id: 'inbox-idea', title: '一个关于灵感的小想法', notebook: 'work', tags: ['待整理'], favorite: false, inbox: true, trashed: false, updatedAt: `${TODAY}T10:12:00+08:00`,
        html: '<p>能不能让笔记在合适的时机，再次回到眼前？</p><p>比如写日记时，轻轻提醒我看看一年前的今天；整理项目时，显示之前留下的相关想法。</p><blockquote>灵感不必马上有用。先留住它。</blockquote>'
      },
      {
        id: 'weekend', title: '周末，去公园走走', notebook: 'life', tags: ['周末', '小计划'], favorite: false, inbox: false, trashed: false, updatedAt: '2026-10-18T16:30:00+08:00',
        html: '<p class="lead">秋天的阳光刚刚好。</p><h2>带上这些就出发</h2><div class="todo-list"><label><input type="checkbox"><span>一本读到一半的书</span></label><label><input type="checkbox"><span>一杯热咖啡</span></label><label><input type="checkbox"><span>一个不用赶时间的下午</span></label></div><p>想拍下树叶的颜色，也想给自己一点发呆的时间。</p>'
      }
    ];
  }

  function sanitizeHTML(html) {
    const template = document.createElement('template');
    template.innerHTML = String(html || '');
    $$('script,style,iframe,object,embed,link,meta,base,svg,math,form,button,textarea,select', template.content).forEach(el => el.remove());
    $$('*', template.content).forEach(el => {
      [...el.attributes].forEach(attr => {
        if (/^on/i.test(attr.name) || /^(srcdoc|style)$/i.test(attr.name)) el.removeAttribute(attr.name);
        if (/^(href|src|xlink:href)$/i.test(attr.name) && !/^(https?:|mailto:|data:image\/(png|jpeg|jpg|gif|webp);base64,|blob:|#|\/)/i.test(attr.value.trim())) el.removeAttribute(attr.name);
      });
      if (el.tagName === 'INPUT' && el.type !== 'checkbox') el.remove();
      if (el.tagName === 'A') el.setAttribute('rel', 'noopener noreferrer');
    });
    return template.innerHTML;
  }

  function plainText(html) {
    const element = document.createElement('div');
    element.innerHTML = html || '';
    return (element.textContent || '').replace(/\s+/g, ' ').trim();
  }

  function toast(message) {
    const element = $('#toast');
    if (!element) return;
    clearTimeout(toastTimer);
    element.textContent = message;
    element.classList.add('is-visible');
    element.hidden = false;
    toastTimer = setTimeout(() => {
      element.classList.remove('is-visible');
      element.hidden = true;
    }, 2500);
  }

  function saveState(text, error = false) {
    const element = $('#save-state');
    if (!element) return;
    element.textContent = text;
    element.classList.toggle('is-error', error);
  }

  function persist() {
    try {
      localStorage.setItem(STORAGE_KEY, JSON.stringify({ notes, selectedId, view }));
      saveState('所有更改已保存');
      return true;
    } catch {
      saveState('保存失败，请导出备份', true);
      toast('浏览器存储空间不足，请先导出当前笔记');
      return false;
    }
  }

  const selectedNote = () => notes.find(note => note.id === selectedId);

  function captureEditor() {
    clearTimeout(saveTimer);
    saveTimer = null;
    const note = selectedNote();
    if (!note || $('#editor-pane')?.hidden) return;
    const title = $('#editor-title')?.value.trim() || '无标题笔记';
    const editor = $('#editor-body');
    if (!editor) return;
    const clone = editor.cloneNode(true);
    $$('input[type="checkbox"]', editor).forEach((input, index) => {
      const copy = $$('input[type="checkbox"]', clone)[index];
      if (input.checked) copy?.setAttribute('checked', '');
      else copy?.removeAttribute('checked');
    });
    const html = sanitizeHTML(clone.innerHTML);
    const changed = note.title !== title || sanitizeHTML(note.html) !== html;
    note.title = title;
    note.html = html;
    if (changed) note.updatedAt = currentStamp();
    if ($('#breadcrumb-current')) $('#breadcrumb-current').textContent = title;
    if ($('#editor-meta')) $('#editor-meta').textContent = `更新于 ${dateLabel(note.updatedAt)} ${note.updatedAt.slice(11, 16)} · ${Math.max(1, Math.ceil(plainText(note.html).length / 300))} 分钟阅读`;
    persist();
    renderList();
    renderOutline();
  }

  function queueSave() {
    saveState('正在保存…');
    clearTimeout(saveTimer);
    saveTimer = setTimeout(captureEditor, 550);
  }

  function visibleNotes() {
    return notes.filter(note => {
      if (view === 'trash' ? !note.trashed : note.trashed) return false;
      const search = query.trim().toLocaleLowerCase();
      if (!search) {
        if (view === 'favorites' && !note.favorite) return false;
        if (view === 'inbox' && !note.inbox) return false;
        if (view === 'today' && !note.updatedAt.startsWith(TODAY)) return false;
        if (['study', 'work', 'life'].includes(view) && note.notebook !== view) return false;
      }
      return !search || `${note.title} ${note.tags.join(' ')} ${plainText(note.html)}`.toLocaleLowerCase().includes(search);
    }).sort((a, b) => Number(b.favorite) - Number(a.favorite) || (sortAscending ? a.updatedAt.localeCompare(b.updatedAt) : b.updatedAt.localeCompare(a.updatedAt)));
  }

  function dateLabel(stamp) {
    const day = stamp.slice(0, 10);
    if (day === TODAY) return '今天';
    if (day === '2026-10-20') return '昨天';
    return `${Number(day.slice(5, 7))}月${Number(day.slice(8, 10))}日`;
  }

  function renderList() {
    const list = $('#note-list');
    if (!list) return;
    const filtered = visibleNotes();
    list.innerHTML = filtered.map(note => `<button class="note-card${note.id === selectedId ? ' is-active' : ''}" data-note-id="${escapeHTML(note.id)}" aria-pressed="${note.id === selectedId}"><span class="note-card-top"><span class="note-icon">${note.id === 'garden' ? '🌱' : notebookIcons[note.notebook] || '📝'}</span><span class="note-card-title">${escapeHTML(note.title)}</span>${note.favorite ? '<span class="star" title="已收藏">☆</span>' : ''}</span><span class="note-card-preview">${escapeHTML(plainText(note.html).slice(0, 82))}</span><span class="note-card-bottom"><span class="note-date">${dateLabel(note.updatedAt)}</span><span class="note-tag">${escapeHTML(note.tags[0] || notebookNames[note.notebook] || '随手记录')}</span></span></button>`).join('');
    if ($('#list-title')) $('#list-title').textContent = query ? '搜索结果' : viewNames[view];
    if ($('#list-count')) $('#list-count').textContent = String(filtered.length);
    if ($('#empty-state')) {
      $('#empty-state').hidden = filtered.length > 0;
      if (!filtered.length) $('#empty-state').textContent = query ? '没有找到相关笔记，换个关键词试试。' : view === 'trash' ? '回收站是空的。' : '这里还没有笔记，写下第一个想法吧。';
    }
    $$('[data-view]').forEach(button => {
      button.classList.toggle('is-active', button.dataset.view === view);
      if (button.dataset.view === view) button.setAttribute('aria-current', 'page');
      else button.removeAttribute('aria-current');
    });
    const counts = {
      all: notes.filter(note => !note.trashed).length,
      inbox: notes.filter(note => !note.trashed && note.inbox).length,
      favorites: notes.filter(note => !note.trashed && note.favorite).length,
      study: notes.filter(note => !note.trashed && note.notebook === 'study').length,
      work: notes.filter(note => !note.trashed && note.notebook === 'work').length,
      life: notes.filter(note => !note.trashed && note.notebook === 'life').length,
      trash: notes.filter(note => note.trashed).length
    };
    $$('[data-count]').forEach(element => { if (counts[element.dataset.count] !== undefined) element.textContent = counts[element.dataset.count]; });
  }

  function renderOutline() {
    const list = $('#outline-list');
    if (!list) return;
    const headings = $$('h2,h3', $('#editor-body') || document.createElement('div'));
    list.innerHTML = headings.length ? headings.map((heading, index) => `<button data-heading-index="${index}" class="outline-link${heading.tagName === 'H3' ? ' is-nested' : ''}">${escapeHTML(heading.textContent)}</button>`).join('') : '<p class="outline-empty">添加标题后，将在这里显示目录。</p>';
  }

  function renderEditor() {
    const note = selectedNote();
    const pane = $('#editor-pane');
    const welcome = $('#welcome-pane');
    if (pane) pane.hidden = !note;
    if (welcome) welcome.hidden = !!note;
    if (!note) return;
    if ($('#editor-title')) $('#editor-title').value = note.title;
    if ($('#editor-body')) $('#editor-body').innerHTML = sanitizeHTML(note.html);
    if ($('#editor-tags')) $('#editor-tags').innerHTML = note.tags.map(tag => `<span class="tag">${escapeHTML(tag)}</span>`).join('');
    if ($('#editor-meta')) $('#editor-meta').textContent = `更新于 ${dateLabel(note.updatedAt)} ${note.updatedAt.slice(11, 16)} · ${Math.max(1, Math.ceil(plainText(note.html).length / 300))} 分钟阅读`;
    if ($('#breadcrumb-current')) $('#breadcrumb-current').textContent = note.title;
    if ($('#breadcrumb-notebook')) $('#breadcrumb-notebook').textContent = notebookNames[note.notebook] || '我的笔记';
    if ($('#favorite-toggle')) {
      $('#favorite-toggle').classList.toggle('is-active', note.favorite);
      $('#favorite-toggle').setAttribute('aria-pressed', String(note.favorite));
      $('#favorite-toggle').title = note.favorite ? '取消收藏' : '收藏笔记';
    }
    if ($('#delete-note')) {
      $('#delete-note').title = note.trashed ? '恢复这篇笔记' : '移入回收站';
      $('#delete-note').setAttribute('aria-label', note.trashed ? '恢复这篇笔记' : '移入回收站');
      const label = $('[data-label]', $('#delete-note'));
      if (label) label.textContent = note.trashed ? '恢复笔记' : '移入回收站';
    }
    if ($('#note-word-count')) $('#note-word-count').textContent = `${plainText(note.html).replace(/\s/g, '').length} 字`;
    saveState('所有更改已保存');
    renderOutline();
  }

  function selectNote(id) {
    if (id !== selectedId) captureEditor();
    selectedId = id;
    closeSlashMenu();
    renderList();
    renderEditor();
    persist();
  }

  function changeView(nextView) {
    captureEditor();
    view = nextView;
    query = '';
    if ($('#search-input')) $('#search-input').value = '';
    const filtered = visibleNotes();
    if (!filtered.some(note => note.id === selectedId)) selectedId = filtered[0]?.id || null;
    renderList();
    renderEditor();
    persist();
  }

  function createNote(options = {}) {
    captureEditor();
    const note = {
      id: globalThis.crypto?.randomUUID?.() || `note-${Date.now()}-${Math.random().toString(16).slice(2)}`,
      title: options.title || '无标题笔记',
      notebook: options.notebook || (['study', 'work', 'life'].includes(view) ? view : 'study'),
      tags: options.tags || [],
      html: options.html || '<p><br></p>',
      updatedAt: currentStamp(), favorite: false, inbox: !!options.inbox, trashed: false
    };
    notes.unshift(note);
    selectedId = note.id;
    if (options.inbox) view = 'inbox';
    else if (options.notebook && view !== 'today') view = note.notebook;
    else if (['trash', 'favorites', 'inbox'].includes(view)) view = 'all';
    query = '';
    if ($('#search-input')) $('#search-input').value = '';
    renderList();
    renderEditor();
    persist();
    setTimeout(() => {
      $('.note-card.is-active')?.scrollIntoView({ block: 'nearest' });
      const title = $('#editor-title');
      title?.focus();
      if (!options.title) title?.select();
      else $('#editor-body')?.focus();
    }, 30);
    return note;
  }

  const templates = {
    daily: { title: '2026年10月21日 · 星期三', notebook: 'life', tags: ['日记'], html: '<p class="lead">今天，也有值得记下的小事。</p><h2>今天的心情</h2><p>用几个词，描述此刻的自己……</p><h2>值得记住的瞬间</h2><p><br></p><h2>今天的小计划</h2><div class="todo-list"><label><input type="checkbox"><span>给自己留一点专注的时间</span></label><label><input type="checkbox"><span>记录一个新的发现</span></label></div><h2>写给明天的自己</h2><p><br></p>' },
    reading: { title: '读书笔记 · 一本新书', notebook: 'study', tags: ['阅读'], html: '<p class="lead">书名：<br>作者：<br>开始阅读的日期：2026年10月21日</p><h2>这本书在回答什么问题？</h2><p><br></p><h2>让我停下来思考的观点</h2><blockquote>记下一段触动你的文字，也写下自己的理解。</blockquote><h2>我想尝试的行动</h2><div class="todo-list"><label><input type="checkbox"><span>用一个小行动，实践书中的想法</span></label></div>' },
    meeting: { title: '会议记录 · 新的讨论', notebook: 'work', tags: ['会议'], html: '<p class="lead">时间：2026年10月21日<br>参与人：<br>会议目标：</p><h2>讨论要点</h2><ul><li>在这里记录大家的想法</li></ul><h2>达成的共识</h2><p><br></p><h2>下一步行动</h2><div class="todo-list"><label><input type="checkbox"><span>行动事项 · 负责人 · 完成时间</span></label></div>' },
    project: { title: '项目计划 · 从一个想法开始', notebook: 'work', tags: ['项目'], html: '<p class="lead">用一句话，说明这个项目想解决的问题。</p><h2>目标与预期结果</h2><p><br></p><h2>里程碑</h2><div class="todo-list"><label><input type="checkbox"><span>明确需求与范围</span></label><label><input type="checkbox"><span>完成第一版原型</span></label><label><input type="checkbox"><span>收集反馈并迭代</span></label></div><h2>资料与想法</h2><p><br></p>' }
  };

  function createDaily() {
    const existing = notes.find(note => !note.trashed && note.tags.includes('日记') && note.title.startsWith('2026年10月21日'));
    if (existing) {
      changeView('all');
      selectNote(existing.id);
      toast('已打开今天的日记');
    } else {
      createNote(templates.daily);
      toast('今天的日记，已经为你准备好了');
    }
  }

  function rememberRange() {
    const selection = window.getSelection();
    if (selection?.rangeCount && $('#editor-body')?.contains(selection.anchorNode)) savedRange = selection.getRangeAt(0).cloneRange();
  }

  function closeSlashMenu() {
    if ($('#slash-menu')) $('#slash-menu').hidden = true;
    $('#add-block')?.setAttribute('aria-expanded', 'false');
  }

  function openSlashMenu(fromSlash = false) {
    const menu = $('#slash-menu');
    const editor = $('#editor-body');
    if (!menu || !editor || !selectedNote()) return;
    rememberRange();
    slashRange = null;
    if (fromSlash && savedRange?.startContainer.nodeType === Node.TEXT_NODE && savedRange.startOffset > 0) {
      const range = savedRange.cloneRange();
      range.setStart(range.startContainer, range.startOffset - 1);
      if (range.toString() === '/') slashRange = range;
    }
    menu.hidden = false;
    const anchor = fromSlash && savedRange ? savedRange.getBoundingClientRect() : $('#add-block')?.getBoundingClientRect();
    const bounds = anchor?.width || anchor?.height ? anchor : editor.getBoundingClientRect();
    menu.style.position = 'fixed';
    menu.style.left = `${Math.max(12, Math.min(bounds.left, window.innerWidth - 310))}px`;
    menu.style.top = `${Math.max(12, Math.min(bounds.bottom + 8, window.innerHeight - Math.min(menu.offsetHeight || 340, 430) - 12))}px`;
    $('#add-block')?.setAttribute('aria-expanded', 'true');
  }

  function insertBlock(type) {
    const editor = $('#editor-body');
    if (!editor || !selectedNote()) return;
    const blocks = {
      paragraph: '<p>写下一点想法…</p>',
      heading: '<h2>新的标题</h2><p><br></p>',
      todo: '<div class="todo-list"><label><input type="checkbox"><span>新的待办事项</span></label></div><p><br></p>',
      quote: '<blockquote>值得记住的一句话…</blockquote><p><br></p>',
      code: '<pre><code>// 在这里写下代码</code></pre><p><br></p>',
      divider: '<hr class="note-divider"><p><br></p>'
    };
    closeSlashMenu();
    editor.focus();
    const selection = window.getSelection();
    if (savedRange && editor.contains(savedRange.startContainer)) {
      selection.removeAllRanges();
      selection.addRange(savedRange);
      if (slashRange) {
        slashRange.deleteContents();
        selection.removeAllRanges();
        selection.addRange(slashRange);
      }
    } else {
      const range = document.createRange();
      range.selectNodeContents(editor);
      range.collapse(false);
      selection.removeAllRanges();
      selection.addRange(range);
    }
    document.execCommand('insertHTML', false, blocks[type] || blocks.paragraph);
    slashRange = null;
    rememberRange();
    queueSave();
    renderOutline();
  }

  function exportMarkdown() {
    captureEditor();
    const note = selectedNote();
    if (!note) return;
    const container = document.createElement('div');
    container.innerHTML = note.html;
    function convert(node) {
      if (node.nodeType === Node.TEXT_NODE) return node.textContent;
      const children = [...node.childNodes].map(convert).join('');
      switch (node.nodeName) {
        case 'H1': return `\n# ${children}\n\n`;
        case 'H2': return `\n## ${children}\n\n`;
        case 'H3': return `\n### ${children}\n\n`;
        case 'P': return `${children}\n\n`;
        case 'BR': return '\n';
        case 'LI': return `- ${children}\n`;
        case 'UL': case 'OL': return `${children}\n`;
        case 'BLOCKQUOTE': return `\n${children.trim().split('\n').map(line => `> ${line}`).join('\n')}\n\n`;
        case 'PRE': return `\n\`\`\`\n${node.textContent}\n\`\`\`\n\n`;
        case 'CODE': return node.parentNode?.nodeName === 'PRE' ? children : `\`${children}\``;
        case 'STRONG': case 'B': return `**${children}**`;
        case 'EM': case 'I': return `*${children}*`;
        case 'HR': return '\n---\n\n';
        case 'LABEL': {
          const checkbox = $('input[type="checkbox"]', node);
          return checkbox ? `- [${checkbox.checked ? 'x' : ' '}] ${node.textContent.trim()}\n` : children;
        }
        case 'INPUT': return '';
        case 'A': return `[${children}](${node.getAttribute('href') || ''})`;
        case 'IMG': return `![${node.getAttribute('alt') || '图片'}](${node.getAttribute('src') || ''})`;
        default: return children;
      }
    }
    const markdown = `# ${note.title}\n\n${note.tags.length ? `标签：${note.tags.join('、')}\n\n` : ''}${[...container.childNodes].map(convert).join('')}`.replace(/\n{4,}/g, '\n\n\n');
    const url = URL.createObjectURL(new Blob([markdown], { type: 'text/markdown;charset=utf-8' }));
    const anchor = document.createElement('a');
    anchor.href = url;
    anchor.download = `${note.title.replace(/[\\/:*?"<>|]/g, '_')}.md`;
    anchor.click();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
    toast('已导出 Markdown 笔记');
  }

  function loadPreferences() {
    let settings = {};
    try { settings = JSON.parse(localStorage.getItem(SETTINGS_KEY) || '{}'); } catch { /* Start with the default appearance. */ }
    const theme = settings.theme === 'dark' ? 'dark' : 'light';
    document.documentElement.dataset.theme = theme;
    document.body.dataset.theme = theme;
    document.body.classList.toggle('dark-mode', theme === 'dark');
    document.body.classList.toggle('is-dark', theme === 'dark');
    $('#theme-toggle')?.setAttribute('aria-pressed', String(theme === 'dark'));
    $('#theme-toggle')?.setAttribute('title', theme === 'dark' ? '切换浅色主题' : '切换深色主题');
    if (settings.fontSize) document.documentElement.style.setProperty('--reading-size', `${settings.fontSize}px`);
    if (settings.lineWidth) document.documentElement.style.setProperty('--reading-width', `${settings.lineWidth}px`);
    if (settings.accent && /^#[\da-f]{6}$/i.test(settings.accent)) document.documentElement.style.setProperty('--accent', settings.accent);
    if ($('#font-size')) $('#font-size').value = settings.fontSize || 16;
    if ($('#line-width')) $('#line-width').value = settings.lineWidth || 760;
    if ($('#accent-color')) $('#accent-color').value = settings.accent || '#7771b5';
    updateSettingLabels();
  }

  function savePreference(key, value) {
    try {
      let settings = {};
      try { settings = JSON.parse(localStorage.getItem(SETTINGS_KEY) || '{}'); } catch { /* Reset malformed preferences. */ }
      settings[key] = value;
      localStorage.setItem(SETTINGS_KEY, JSON.stringify(settings));
      loadPreferences();
    } catch { toast('暂时无法保存外观设置'); }
  }

  function updateSettingLabels() {
    if ($('#font-size-value')) $('#font-size-value').textContent = `${$('#font-size')?.value || 16}px`;
    if ($('#line-width-value')) $('#line-width-value').textContent = `${$('#line-width')?.value || 760}px`;
  }

  function openDialog(id) {
    const dialog = $(id);
    if (!dialog) return;
    if (typeof dialog.showModal === 'function') dialog.showModal();
    else dialog.setAttribute('open', '');
  }

  function init() {
    const seeds = seedNotes();
    try {
      const stored = JSON.parse(localStorage.getItem(STORAGE_KEY) || 'null');
      if (stored && Array.isArray(stored.notes)) {
        notes = stored.notes.filter(note => note && typeof note.id === 'string' && typeof note.title === 'string' && typeof note.html === 'string').map(note => ({ ...note, tags: Array.isArray(note.tags) ? note.tags : [], html: sanitizeHTML(note.html) }));
        selectedId = stored.selectedId;
        view = Object.hasOwn(viewNames, stored.view) ? stored.view : 'all';
      } else notes = seeds;
    } catch { notes = seeds; }
    if (!notes.length) notes = seeds;
    if (!selectedNote()) selectedId = visibleNotes()[0]?.id || null;
    loadPreferences();
    renderList();
    renderEditor();

    $('#note-list')?.addEventListener('click', event => {
      const card = event.target.closest('[data-note-id]');
      if (card) selectNote(card.dataset.noteId);
    });
    document.addEventListener('click', event => {
      const navigation = event.target.closest('[data-view]');
      if (navigation) changeView(navigation.dataset.view);
      const tag = event.target.closest('[data-search-tag]');
      if (tag) {
        captureEditor();
        view = 'all';
        query = tag.dataset.searchTag;
        if ($('#search-input')) $('#search-input').value = query;
        const filtered = visibleNotes();
        selectedId = filtered[0]?.id || null;
        renderList();
        renderEditor();
        persist();
      }
      const template = event.target.closest('[data-template]');
      if (template && templates[template.dataset.template]) {
        $('#template-dialog')?.close?.();
        if (template.dataset.template === 'daily') createDaily();
        else createNote(templates[template.dataset.template]);
        toast('模板已准备好，开始记录吧');
      }
      const close = event.target.closest('[data-close-dialog], [data-dialog-close], .dialog-close');
      if (close) close.closest('dialog')?.close();
      const block = event.target.closest('[data-block]');
      if (block) insertBlock(block.dataset.block);
      else if (!event.target.closest('#slash-menu, #add-block, #editor-body')) closeSlashMenu();
      const outline = event.target.closest('[data-heading-index]');
      if (outline) $$('h2,h3', $('#editor-body'))[Number(outline.dataset.headingIndex)]?.scrollIntoView({ behavior: 'smooth', block: 'center' });
    });
    $('#new-note')?.addEventListener('click', () => createNote());
    $('#quick-note')?.addEventListener('click', () => { createNote({ inbox: true, title: '随手记下一个想法', tags: ['待整理'] }); toast('已创建一条收集箱笔记'); });
    $('#daily-note')?.addEventListener('click', createDaily);
    $('#search-input')?.addEventListener('input', event => {
      query = event.target.value;
      renderList();
    });
    $('#sort-toggle')?.addEventListener('click', () => {
      sortAscending = !sortAscending;
      $('#sort-toggle').firstChild.textContent = sortAscending ? '最早编辑' : '最近编辑';
      $('#sort-toggle').setAttribute('aria-pressed', String(sortAscending));
      renderList();
    });
    $('#editor-title')?.addEventListener('input', queueSave);
    $('#editor-title')?.addEventListener('keydown', event => { if (event.key === 'Enter') { event.preventDefault(); $('#editor-body')?.focus(); } });
    const editor = $('#editor-body');
    editor?.addEventListener('input', () => {
      if (!composing) queueSave();
      if ($('#note-word-count')) $('#note-word-count').textContent = `${plainText(editor.innerHTML).replace(/\s/g, '').length} 字`;
    });
    editor?.addEventListener('change', event => { if (event.target.matches('input[type="checkbox"]')) queueSave(); });
    editor?.addEventListener('compositionstart', () => { composing = true; clearTimeout(saveTimer); });
    editor?.addEventListener('compositionend', () => { composing = false; queueSave(); });
    editor?.addEventListener('keyup', rememberRange);
    editor?.addEventListener('mouseup', rememberRange);
    editor?.addEventListener('keydown', event => {
      if (event.key === '/' && !event.isComposing && !composing && !event.ctrlKey && !event.metaKey) setTimeout(() => openSlashMenu(true), 0);
      if (event.key === 'Tab' && !event.isComposing) {
        const selection = window.getSelection();
        const anchor = selection?.anchorNode?.nodeType === Node.ELEMENT_NODE ? selection.anchorNode : selection?.anchorNode?.parentElement;
        if (anchor?.closest('pre')) { event.preventDefault(); document.execCommand('insertText', false, '    '); queueSave(); }
      }
    });
    editor?.addEventListener('paste', event => {
      const clipboard = event.clipboardData;
      if (!clipboard) return;
      const image = [...clipboard.items].find(item => item.type.startsWith('image/'));
      if (image) {
        event.preventDefault();
        const reader = new FileReader();
        reader.onload = () => { document.execCommand('insertHTML', false, `<p><img src="${escapeHTML(reader.result)}" alt="粘贴的图片"></p><p><br></p>`); queueSave(); };
        const file = image.getAsFile();
        if (file) reader.readAsDataURL(file);
        return;
      }
      const html = clipboard.getData('text/html');
      if (html) { event.preventDefault(); document.execCommand('insertHTML', false, sanitizeHTML(html)); queueSave(); }
    });
    $('#favorite-toggle')?.addEventListener('click', () => {
      const note = selectedNote();
      if (!note) return;
      captureEditor();
      note.favorite = !note.favorite;
      $('#favorite-toggle').classList.toggle('is-active', note.favorite);
      $('#favorite-toggle').setAttribute('aria-pressed', String(note.favorite));
      $('#favorite-toggle').title = note.favorite ? '取消收藏' : '收藏笔记';
      renderList();
      persist();
      toast(note.favorite ? '已收藏，可以在「我的收藏」里找到' : '已取消收藏');
    });
    $('#delete-note')?.addEventListener('click', () => {
      const note = selectedNote();
      if (!note) return;
      captureEditor();
      note.trashed = !note.trashed;
      note.updatedAt = currentStamp();
      toast(note.trashed ? '已移入回收站，可随时恢复' : '笔记已恢复');
      selectedId = visibleNotes()[0]?.id || null;
      renderList();
      renderEditor();
      persist();
    });
    $('#theme-toggle')?.addEventListener('click', () => savePreference('theme', document.documentElement.dataset.theme === 'dark' ? 'light' : 'dark'));
    $('#focus-toggle')?.addEventListener('click', () => {
      const active = document.body.classList.toggle('is-focus');
      $('#focus-toggle').setAttribute('aria-pressed', String(active));
      toast(active ? '专注模式 · 按 Esc 返回' : '已退出专注模式');
    });
    $('#outline-toggle')?.addEventListener('click', () => {
      const active = document.body.classList.toggle('show-outline');
      $('#outline-toggle').classList.toggle('is-active', active);
      $('#outline-toggle').setAttribute('aria-pressed', String(active));
      renderOutline();
    });
    $('#add-block')?.addEventListener('mousedown', event => { event.preventDefault(); rememberRange(); });
    $('#add-block')?.addEventListener('click', () => $('#slash-menu')?.hidden ? openSlashMenu() : closeSlashMenu());
    $('#slash-menu')?.addEventListener('mousedown', event => event.preventDefault());
    $('#export-note')?.addEventListener('click', exportMarkdown);
    $('#template-toggle')?.addEventListener('click', () => openDialog('#template-dialog'));
    $('#settings-toggle')?.addEventListener('click', () => openDialog('#settings-dialog'));
    $('#font-size')?.addEventListener('input', event => savePreference('fontSize', Math.max(14, Math.min(22, Number(event.target.value)))));
    $('#line-width')?.addEventListener('input', event => savePreference('lineWidth', Math.max(560, Math.min(960, Number(event.target.value)))));
    $('#accent-color')?.addEventListener('input', event => savePreference('accent', event.target.value));
    $$('dialog').forEach(dialog => dialog.addEventListener('click', event => { if (event.target === dialog) { const rect = dialog.getBoundingClientRect(); if (event.clientX < rect.left || event.clientX > rect.right || event.clientY < rect.top || event.clientY > rect.bottom) dialog.close(); } }));
    document.addEventListener('keydown', event => {
      if (event.isComposing || composing) return;
      if ((event.ctrlKey || event.metaKey) && !event.shiftKey && event.key.toLowerCase() === 'n') { event.preventDefault(); $('#quick-note')?.click(); }
      if ((event.ctrlKey || event.metaKey) && event.shiftKey && event.key.toLowerCase() === 'f') { event.preventDefault(); $('#search-input')?.focus(); $('#search-input')?.select(); }
      if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === 's') { event.preventDefault(); captureEditor(); toast('笔记已保存到当前浏览器'); }
      if (event.key === 'Escape') { closeSlashMenu(); document.body.classList.remove('is-focus'); $('#focus-toggle')?.setAttribute('aria-pressed', 'false'); }
    });
    document.addEventListener('visibilitychange', () => { if (document.hidden && saveTimer) captureEditor(); });
    window.addEventListener('beforeunload', () => { if (saveTimer) captureEditor(); });
    window.addEventListener('resize', closeSlashMenu);
  }

  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', init);
  else init();
})();
