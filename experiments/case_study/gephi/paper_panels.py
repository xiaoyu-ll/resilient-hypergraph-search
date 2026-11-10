"""Publication rendering of unchanged Gephi ForceAtlas2 coordinates."""
from pathlib import Path
import csv,json,sys
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.lines import Line2D
R=Path(__file__).resolve().parents[3];P=Path(__file__).resolve().parent
sys.path.insert(0,str(Path.home()/'.codex/skills/scipilot-figure-skill/scripts'))
from setup_style import setup_style
from visual_qa import audit_layout
setup_style(journal='ieee',lang='en',use_sciplots=False,constrained_layout=False)
plt.rcParams.update({'font.family':'DejaVu Sans','font.size':6.5,'pdf.fonttype':42,'svg.fonttype':'none'})
rows=list(csv.DictReader((P/'positions.tsv').open(),delimiter='\t'));pos={r['id']:(float(r['x']),float(r['y'])) for r in rows}
links=[(r['source'],r['target']) for r in csv.DictReader((P/'edges.tsv').open(),delimiter='\t')]
codes=dict(zip(['6466','6467','6527','6765','30517','63021','88621','107893','134496','139584','197410'],['Nas','Has','Bou','Mou','Boc','Maa','Tal','Haf','Kha','Rah','Tah']))
failed='a6765';groups={b for a,b in links if a==failed};removed=groups|{failed}
xs=[x for x,y in pos.values()];ys=[y for x,y in pos.values()];dx=max(xs)-min(xs);dy=max(ys)-min(ys)
QA=R/'output/figure_qa/case_study/gephi';QA.mkdir(parents=True,exist_ok=True)
for after,stage in [(False,'before'),(True,'after')]:
 fig=plt.figure(figsize=(.49*241.14749/72.27,1.10),layout='none');ax=fig.add_axes([0,0,1,1]);ax.set(xlim=(min(xs)-.24*dx,max(xs)+.26*dx),ylim=(min(ys)-.16*dy,max(ys)+.16*dy));ax.axis('off')
 for a,b in links:
  if after and (a in removed or b in removed):continue
  ax.plot([pos[a][0],pos[b][0]],[pos[a][1],pos[b][1]],color='#888888',lw=.7,zorder=0)
 for r in rows:
  n=r['id'];x,y=pos[n];author=r['kind']=='author';seed=n in ['a6765','a139584']
  if after and n in groups:continue
  if after and n==failed:
   ax.plot(x,y,'x',color='#999999',ms=6,zorder=3);continue
  ax.plot(x,y,marker='s' if seed else ('o' if author else 'D'),mfc='#D55E00' if seed else ('white' if author else '#888888'),mec='#D55E00' if seed else '#555555',ms=4.5 if author else 2.3,mew=.6,zorder=2)
  if author:
   offsets={'Nas':(0,7),'Tal':(-4,-7),'Rah':(-2,-8),'Bou':(0,8),'Kha':(0,-7),'Haf':(0,8),'Boc':(-4,-9),'Maa':(8,1),'Tah':(-5,8),'Has':(10,2),'Mou':(0,9)}
   ax.annotate(codes[n[1:]],(x,y),xytext=offsets[codes[n[1:]]],textcoords='offset points',ha='center',va='center',fontsize=6.5,color='#D55E00' if seed else '#222222',bbox=dict(facecolor='white',edgecolor='none',pad=.1),zorder=3)
 issues=audit_layout(fig);assert not issues,issues
 fig.savefig(QA/(stage+'.png'),dpi=400);fig.savefig(R/'paper/figures'/f'case_dblp_{stage}.pdf');fig.savefig(R/'paper/figures'/f'case_dblp_{stage}.svg');plt.close(fig)
fig=plt.figure(figsize=(.78*241.14749/72.27,.17),layout='none')
fig.legend(handles=[Line2D([],[],ls='',marker=m,mfc=c,mec='#555555',color='#888888',ms=4,label=l) for m,c,l in [('s','#D55E00','Query'),('o','white','Author'),('D','#888888','Group'),('x','white','Failed')]],loc='center',ncol=4,frameon=False,fontsize=6.5,handletextpad=.3,columnspacing=.6)
fig.savefig(R/'paper/figures/case_dblp_legend.pdf');plt.close(fig)
(P/'incidence_audit.json').write_text(json.dumps(dict(before_nodes=len(rows),before_links=len(links),after_nodes=len(rows)-len(removed),after_links=sum(a not in removed and b not in removed for a,b in links),layout='Gephi ForceAtlas2',same_coordinates=True,rendering='Matplotlib publication styling; no layout changes'),indent=2)+'\n')
