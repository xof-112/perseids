#!/usr/bin/env python3
"""Writes the appendix of the guide (pages + mod destinations) from the
plug-in's own tables:  ./test/sim --list | python3 docs/appendix.py docs/perseids-nt-anleitung.html
The appendix sits between <!-- appendix:start --> and <!-- appendix:end -->."""
import html
import re
import sys

pages, order, targets = {}, [], []
for line in sys.stdin:
    kind, a, b = line.rstrip("\n").split("\t")
    if kind == "P":
        if a not in pages:
            pages[a] = []
            order.append(a)
        pages[a].append(b)
    elif kind == "T":
        targets.append((int(a), b))

# Where each parameter lives (first page that lists it; Mod overview skipped).
home = {}
for pg in order:
    for name in pages[pg]:
        home.setdefault(name, pg)


def e(t):
    return html.escape(t)


rows = []
mod_pages = [p for p in order if re.fullmatch(r"Mod \d+", p)]
for pg in order:
    if pg in mod_pages[1:]:
        continue
    if pg == "Mod 1":
        label = "Mod 1 … Mod %d" % len(mod_pages)
        params = " · ".join(e("Mod n (Amount)" if n == "Mod 1" else n.replace("Mod 1 ", "Mod n ")) for n in pages[pg])
    else:
        label = pg
        params = " · ".join(e(n) for n in pages[pg])
    rows.append("  <tr><td>%s</td><td>%s</td></tr>" % (e(label), params))

trows = []
for idx, name in targets:
    if idx == 0:
        where = "schaltet den Slot ab"
    else:
        where = home.get(name, "")
    trows.append("  <tr><td>%d</td><td>%s</td><td>%s</td></tr>" % (idx, e(name), e(where)))

block = """<!-- appendix:start -->
<article class="patch-card" id="anhang">
  <div class="ph"><div class="num">12</div><div class="ph-main"><h2>Anhang: Wo steht was</h2></div></div>
  <div class="pb">
  <p>Erzeugt aus den Tabellen des Plug-ins (<code>./test/sim --list | python3 docs/appendix.py …</code>), passt also immer zur Version.</p>
  <h3>Parameterseiten in Menü-Reihenfolge</h3>
  <div class="tbl"><table><tr><th>Seite</th><th>Parameter</th></tr>
%s
  </table></div>
  <h3>Mod-Ziele (Mod n dest)</h3>
  <p><b>Nr.</b> = Rasten von <i>None</i> aus nach rechts. Beispiel: <i>Dry/Wet</i> ist Nr. %d. Die Ziele stehen in der Reihenfolge der Parameterseiten, die Amount/Offset/Rate-Ziele der Slots („Mod the mod“) am Ende.</p>
  <div class="tbl"><table><tr><th>Nr.</th><th>Ziel</th><th>Seite</th></tr>
%s
  </table></div>
  <a class="backtop" href="#inhalt">↑ zum Inhalt</a></div>
</article>
<!-- appendix:end -->""" % ("\n".join(rows), dict((n, i) for i, n in targets).get("Dry/Wet", -1), "\n".join(trows))

path = sys.argv[1]
doc = open(path, encoding="utf-8").read()
if "<!-- appendix:start -->" in doc:
    doc = re.sub(r"<!-- appendix:start -->.*?<!-- appendix:end -->", lambda m: block, doc, flags=re.S)
else:
    doc = doc.replace("</section>\n<footer>", block + "\n</section>\n<footer>", 1)
toc = '<li><a class="tl" href="#anhang"><span class="tn">12</span><span class="tt">Anhang: Wo steht was</span></a></li>'
if toc not in doc:
    doc = re.sub(r'(<li><a class="tl" href="#grenzen">.*?</li>)', lambda m: m.group(1) + "\n" + toc, doc, count=1, flags=re.S)
open(path, "w", encoding="utf-8").write(doc)
print("appendix: %d pages, %d targets" % (len(rows), len(targets)))
