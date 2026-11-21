"""Render reviewed holdout-01 evidence. Usage: report.py RUN_DIRECTORY OUTPUT.pdf (ReportLab, Pillow)."""
import json, sys
from pathlib import Path
from PIL import Image
from reportlab.pdfgen.canvas import Canvas
from reportlab.lib.colors import HexColor
from reportlab.lib.utils import ImageReader

run=Path(sys.argv[1]); ref=json.loads((run/'reference.json').read_text()); native=json.loads((run/'litebird.json').read_text());
if not (len(native)==4 and not any(x['pass'] for x in native) and len(ref['rows'])==4 and all(x['pass'] for x in ref['rows'])):
    sys.exit('These captions describe the first holdout run only; review new results before publication.')
c=Canvas(sys.argv[2],pagesize=(842,606)); c.setTitle('Litebird | Four unseen browsing tasks')
def text(x,y,msg,size=12,bold=False,color='#182f3b'):
    c.setFillColor(HexColor(color));c.setFont('Helvetica-Bold' if bold else 'Helvetica',size);c.drawString(x,y,msg)
def title(label,heading,desc):
    text(30,572,label,10,True,'#216766');text(30,531,heading,26,True);text(30,503,desc,12)
def pair(id,stage):
    for i,(pre,label) in enumerate([('ref','Chromium 131'),('lite','Litebird')]):
        x=30+i*395;text(x,471,label,13,True);im=Image.open(run/f'{id}-{pre}-{stage}.png').convert('RGB');c.drawImage(ImageReader(im),x,171,width=384,height=288);c.setStrokeColor(HexColor('#cad6d6'));c.rect(x,171,384,288)
title('LITEBIRD   /   HOLDOUT 01','Four new pages, no engine fixes','A transfer test on the exact engine build used for the 20/20 development set.')
for x,metric,caption in [(30,'4/4','Chromium completed'),(296,'0/4','Litebird strict checks'),(565,'1/4','Litebird visible completions')]:
    text(x,428,metric,34,True,'#216766');text(x,405,caption,13)
text(30,352,'Why two Litebird scores?',16,True);text(30,328,'The APG tab switch worked for the reader, but the DOM said the visible biography was below the viewport.');text(30,306,'The failing check is retained. Three other tasks stopped short of the requested outcome; no partial credit.')
for i,s in enumerate(['H1  Rust Book: no clickable Next chapter link.','H2  W3C tabs: keyboard selection and reading worked; geometry and layout differed.','H3  MDN dialog: Cancel returned the right value, but the closed dialog was still painted.','H4  TodoMVC: added the item; could not click its completion toggle.']):text(30,260-26*i,s)
text(30,116,'Protocol',14,True);text(30,94,'Freeze 25374e7; engine 771fb6f. Same pinned assets, separate origin-isolated sessions, real input, 800 x 600.');text(30,73,'No page substitutions or engine fixes. The original 20 tasks are a different set, not an additional denominator.');c.showPage()
for id,name,desc,stage,verdict,lines in [
    ('H1','Rust Book: Next chapter','Click Next from Getting Started to reach Installation.','after','BLOCKED',['Both native Next links had zero height: 760 x 0 and 0 x 0. Neither supplied a hit target.','Litebird remained on Getting Started. Different destinations: do not score these frames as a rendering comparison.']),
    ('H2','W3C APG: switch tabs','Click the initially selected tab and press ArrowRight once.','after','VISIBLE ACTION COMPLETED  /  STRICT AND VISUAL CHECKS FAILED',['Carl Andersen is focused and selected, panel 1 is hidden, and the new biography is legible in the frame.','The biography reports y=2061 with window.scrollY=0. Native tabs stack vertically.','259,542 / 480,000 pixels differ at this equivalent outcome; no tolerance allowed.']),
    ('H3','MDN: open the animal dialog','Intermediate frame after clicking Update details.','open','PARTIAL: OPEN FLAG IS NOT PROOF OF A MODAL',['Chromium draws a centered dialog over a backdrop. Litebird paints the form inline.','Litebird was already painting the dialog contents before it was opened.']),
    ('H3','MDN: cancel the dialog','Final frame after clicking Cancel.','after','INCOMPLETE',['Both returned animalNotChosen. Only Chromium hid the form and restored focus to Update details.','Litebird left the form painted and focus on Cancel. Different end states.']),
    ('H4','TodoMVC: add and complete one item','Click the input, type Buy mangoes, press Enter, then click the item toggle.','after','INCOMPLETE',['Litebird added one matching item and cleared the input. The checkbox center hit a label instead.','The driver did not redirect the click. Chromium showed zero items left; Litebird still showed one.'])]:
    title('HOLDOUT 01   /   '+id,name,desc);pair(id,stage);text(30,140,verdict,12,True,'#88512d');
    for i,s in enumerate(lines): text(30,114-19*i,s,11)
    c.showPage()
c.save()
