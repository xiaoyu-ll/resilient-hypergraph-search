"""Style the actual Gephi coordinates; projected edges are visualization only."""
from pathlib import Path
import csv,json,sys
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.lines import Line2D
R=Path(__file__).resolve().parents[4];P=Path(__file__).resolve().parent
sys.path.insert(0,str(Path.home()/'.codex/skills/scipilot-figure-skill/scripts'))
from setup_style import setup_style
from visual_qa import audit_layout
setup_style(journal='ieee',lang='en',use_sciplots=False,constrained_layout=False)
plt.rcParams.update({'font.family':'DejaVu Sans','font.size':6.5,'pdf.fonttype':42,'svg.fonttype':'none'})
meta=json.loads((P.parent/'senate_labels.json').read_text());qa=R/'output/figure_qa/case_study/senate';qa.mkdir(parents=True,exist_ok=True)
for stage in ['mixed','same_party']:
 p=P/stage;rows=list(csv.DictReader((p/'positions.tsv').open(),delimiter='\t'));pos={r['id']:(float(r['x']),float(r['y'])) for r in rows};links=list(csv.DictReader((p/'edges.tsv').open(),delimiter='\t'))
 xs=[x for x,y in pos.values()];ys=[y for x,y in pos.values()];dx=max(xs)-min(xs);dy=max(ys)-min(ys)
 fig=plt.figure(figsize=(.49*241.14749/72.27,1.10),layout='none');ax=fig.add_axes([0,0,1,1]);ax.set(xlim=(min(xs)-.22*dx,max(xs)+.22*dx),ylim=(min(ys)-.24*dy,max(ys)+.24*dy));ax.axis('off')
 for e in links:
  a,b=pos[e['source']],pos[e['target']];ax.plot([a[0],b[0]],[a[1],b[1]],lw=.35+.17*int(e['weight']),color='#888888',alpha=.65,zorder=0)
 for r in rows:
  v=int(r['id'][1:]);x,y=pos[r['id']];seed=v in [150,191];party=meta[str(v)]['party'];color='#D55E00' if party=='Republican' else '#0072B2'
  ax.plot(x,y,marker='s' if seed else ('D' if party=='Republican' else 'o'),ms=13,mfc=color,mec='black' if seed else color,mew=1 if seed else .7,zorder=2)
  ax.text(x,y,meta[str(v)]['name'].title()[:3],fontsize=6.5,ha='center',va='center',color='white',zorder=3)
 issues=audit_layout(fig);assert not issues,issues
 fig.savefig(qa/(stage+'.png'),dpi=400);fig.savefig(R/'paper/figures'/f'case_senate_{stage}.pdf');fig.savefig(R/'paper/figures'/f'case_senate_{stage}.svg');plt.close(fig)
fig=plt.figure(figsize=(.78*241.14749/72.27,.17),layout='none')
fig.legend(handles=[Line2D([],[],ls='',marker=m,mfc=c,mec='black' if l=='Query' else c,ms=5,label=l) for m,c,l in [('o','#0072B2','Democrat'),('D','#D55E00','Republican'),('s','white','Query')]],loc='center',ncol=3,frameon=False,fontsize=6.5,handletextpad=.3,columnspacing=.7)
fig.savefig(R/'paper/figures/case_senate_legend.pdf');plt.close(fig)
