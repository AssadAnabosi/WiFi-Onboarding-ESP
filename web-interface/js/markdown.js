// Release notes markdown for the Settings Firmware card. Loaded only by settings.html,
// before firmware.js (and before utils/ui_mock.js in the preview, which keeps this file).

/**
 * Release notes (the CHANGELOG section) as markdown into `box`: headings, nested "-"/"*" lists,
 * paragraphs, `code`, **bold**, *italic* and [links](https://…). Built with DOM nodes and
 * textContent only, so nothing in the notes is ever parsed as HTML; anything else, or an
 * unclosed marker (notes are cut at 4 KB), shows as plain text.
 */
function renderMarkdown(box, text) {
  box.textContent = "";
  let lists = []; // open lists, outermost first: {indent, ul}
  let block = null; // paragraph or list item that a plain line continues
  String(text || "")
    .split(/\r?\n/)
    .forEach(function (line) {
      let heading = line.match(/^ {0,3}(#{1,6})\s+(.*?)\s*#*\s*$/);
      let item = line.match(/^(\s*)[-*+]\s+(.*)$/);
      if (!line.trim()) {
        block = null;
      } else if (heading) {
        lists = [];
        block = null;
        box.appendChild(mdInline(document.createElement("h4"), heading[2]));
      } else if (item) {
        let indent = item[1].replace(/\t/g, "  ").length;
        while (lists.length && indent < lists[lists.length - 1].indent) lists.pop();
        let top = lists[lists.length - 1];
        if (!top || indent > top.indent + 1) {
          let ul = document.createElement("ul");
          let parent = top && top.ul.lastChild;
          (parent || box).appendChild(ul);
          lists.push((top = { indent: indent, ul: ul }));
        }
        block = top.ul.appendChild(mdInline(document.createElement("li"), item[2]));
      } else if (block) {
        mdInline(block, " " + line.trim());
      } else {
        lists = [];
        block = box.appendChild(mdInline(document.createElement("p"), line.trim()));
      }
    });
}

// Appends `text` to `el`, turning `code`, **bold**, *italic*/_italic_ and [text](http…) into elements
function mdInline(el, text) {
  let pattern = /`([^`]+)`|\*\*(.+?)\*\*|\*([^*\s][^*]*?)\*|\b_([^_]+)_\b|\[([^\]]+)\]\((https?:\/\/[^\s)]+)\)/g;
  let last = 0;
  let m;
  while ((m = pattern.exec(text))) {
    el.appendChild(document.createTextNode(text.slice(last, m.index)));
    let node;
    if (m[1]) {
      node = document.createElement("code");
      node.textContent = m[1];
    } else if (m[2]) {
      node = mdInline(document.createElement("strong"), m[2]);
    } else if (m[3] || m[4]) {
      node = mdInline(document.createElement("em"), m[3] || m[4]);
    } else {
      node = mdInline(document.createElement("a"), m[5]);
      node.href = m[6];
      node.target = "_blank";
      node.rel = "noopener noreferrer";
    }
    el.appendChild(node);
    last = pattern.lastIndex;
  }
  el.appendChild(document.createTextNode(text.slice(last)));
  return el;
}
