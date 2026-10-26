(() => {
  const paths = {
    leaf:'<path d="M20 4c-6-1-13 0-15 6-2 6 4 10 9 7 4-2 5-7 6-13Z"/><path d="M4 21c1-6 5-10 11-12"/>',
    sprout:'<path d="M12 21v-9M12 16C5 16 3 12 3 7c6-1 10 3 9 9ZM12 12c0-6 4-9 9-9 1 6-3 10-9 9Z"/>',
    plus:'<path d="M12 5v14M5 12h14"/>',sun:'<circle cx="12" cy="12" r="4"/><path d="M12 2v2M12 20v2M2 12h2M20 12h2M5 5l1.5 1.5M17.5 17.5 19 19M5 19l1.5-1.5M17.5 6.5 19 5"/>',
    inbox:'<path d="m4 4-2 11v5h20v-5L20 4Z"/><path d="M2 15h6l2 3h4l2-3h6"/>',layers:'<path d="m12 3 10 5-10 5L2 8l10-5ZM2 12l10 5 10-5M2 16l10 5 10-5"/>',
    star:'<path d="m12 3 2.8 5.8 6.4.9-4.6 4.5 1.1 6.3-5.7-3-5.7 3 1.1-6.3-4.6-4.5 6.4-.9L12 3Z"/>',
    'book-open':'<path d="M12 5C9 3 5 3 2 4v15c4-1 7-1 10 1 3-2 6-2 10-1V4c-3-1-7-1-10 1Zm0 0v15"/>',
    'arrow-right':'<path d="M4 12h16m-6-6 6 6-6 6"/>',trash:'<path d="M3 6h18M9 6V3h6v3M5 6l1 15h12l1-15M10 10v7M14 10v7"/>',
    settings:'<path d="m9 3-.7 3-2.7 1-2.6-.6-1 3 2.2 2v3l-2.2 2 1.5 2.5 2.7-.7 2.6 1 .7 2.8h3l.8-2.8 2.7-1 2.6.7 1.5-2.6-2.2-2v-3l2.2-2-1.5-2.6-2.7.6-2.6-1L12.5 3Z"/><circle cx="10.5" cy="12" r="3"/>',
    'square-pen':'<path d="M12 4H4v16h16v-8M9 15l1-5L19 1l4 4-9 9-5 1ZM17 3l4 4"/>',search:'<circle cx="10.5" cy="10.5" r="6.5"/><path d="m16 16 5 5"/>',
    'arrow-down-wide':'<path d="M4 4v16m-3-3 3 3 3-3M11 5h10M11 10h7M11 15h4M11 20h1"/>',lock:'<rect x="5" y="10" width="14" height="11" rx="2"/><path d="M8 10V7a4 4 0 0 1 8 0v3M12 14v3"/>',
    'list-tree':'<path d="M4 3v12h5M4 9h5M13 5h8M13 9h6M13 15h8M13 19h6M4 15v4h5"/>',expand:'<path d="M8 3H3v5M16 3h5v5M3 16v5h5M21 16v5h-5"/>',
    download:'<path d="M12 3v12m-5-5 5 5 5-5M4 16v5h16v-5"/>',layout:'<rect x="3" y="3" width="18" height="18" rx="2"/><path d="M3 9h18M9 9v12"/>',
    feather:'<path d="M20 3c-8-3-13 1-14 8l-1 7 7-1c7-2 10-7 8-14ZM3 21 15 9M8 16v-5M8 16h5"/>',text:'<path d="M4 5h16M12 5v15M8 20h8"/>',
    'check-square':'<path d="m8 11 4 4 9-10M20 12v8H3V3h12"/>',quote:'<path d="M3 6h7v7H5c0 3 1 4 4 5M14 6h7v7h-5c0 3 1 4 4 5"/>',
    code:'<path d="m8 6-6 6 6 6M16 6l6 6-6 6M14 3l-4 18"/>',minus:'<path d="M4 12h16"/>',x:'<path d="m6 6 12 12M6 18 18 6"/>',moon:'<path d="M20 14A9 9 0 0 1 10 3a9 9 0 1 0 10 11Z"/>',
    file:'<path d="M14 2H5v20h14V7l-5-5Zm0 0v5h5M8 12h8M8 16h6"/>',pin:'<path d="m9 3 9 3-3 4v5l-4-1-5 2 1-6 2-7ZM10 15l-4 7"/>'
  };
  window.icons = Object.fromEntries(Object.entries(paths).map(([name, path]) => [name, `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.65" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${path}</svg>`]));
  document.querySelectorAll('[data-icon]').forEach(el => { el.innerHTML = window.icons[el.dataset.icon] || window.icons.file; });
})();
