import java.io.*;
import java.nio.file.*;
import java.awt.Color;
import java.awt.Font;
import java.util.*;
import org.openide.util.Lookup;
import org.gephi.project.api.*;
import org.gephi.graph.api.*;
import org.gephi.layout.plugin.forceAtlas2.ForceAtlas2;
import org.gephi.preview.api.*;
import org.gephi.io.exporter.api.ExportController;
public class RenderSenate {
 static void waitSave(File f) throws Exception {long last=-1;int stable=0;for(int i=0;i<100;i++){Thread.sleep(100);long n=f.length();if(n>500&&n==last)stable++;else stable=0;if(stable>=5)return;last=n;}throw new IOException("Gephi project not saved: "+f);}
 public static void main(String[] args) throws Exception {
  File dir=new File(args[0]);
  ProjectController pc=Lookup.getDefault().lookup(ProjectController.class);pc.newProject();Workspace ws=pc.getCurrentWorkspace();
  GraphModel gm=Lookup.getDefault().lookup(GraphController.class).getGraphModel(ws);UndirectedGraph g=gm.getUndirectedGraph();
  gm.getNodeTable().addColumn("party",String.class);gm.getNodeTable().addColumn("query",Boolean.class);gm.getNodeTable().addColumn("kind",String.class);gm.getNodeTable().addColumn("full_name",String.class);
  Random random=new Random(20261106);
  for(String line:Files.readAllLines(new File(dir,"nodes.tsv").toPath()).subList(1,Files.readAllLines(new File(dir,"nodes.tsv").toPath()).size())) {
   String[] a=line.split("\t");Node n=gm.factory().newNode(a[0]);n.setLabel(a[1]);n.setAttribute("full_name",a[1]);n.setAttribute("kind",a[2]);n.setAttribute("party",a[4]);n.setAttribute("query",a[3].equals("1"));
   n.setX(random.nextFloat()*200-100);n.setY(random.nextFloat()*200-100);
   boolean author=a[2].equals("author"),seed=a[3].equals("1");n.setSize(seed?12f:(author?8f:3.2f));n.setColor(a[4].equals("Republican")?new Color(213,94,0):new Color(0,114,178));g.addNode(n);
  }
  List<String> input=Files.readAllLines(new File(dir,"edges.tsv").toPath());
  for(String line:input.subList(1,input.size())){String[] a=line.split("\t");Edge e=gm.factory().newEdge(g.getNode(a[0]),g.getNode(a[1]),false);e.setWeight(Double.parseDouble(a[2]));e.setColor(new Color(155,155,155));g.addEdge(e);}
  ForceAtlas2 fa=new ForceAtlas2(null);fa.setGraphModel(gm);fa.resetPropertiesValues();fa.setThreadsCount(1);fa.setScalingRatio(60.0);fa.setGravity(1.0);fa.setAdjustSizes(true);fa.initAlgo();for(int i=0;i<2500;i++)fa.goAlgo();fa.endAlgo();
  // Freeze one layout for both snapshots; deleting a member must not rearrange the community.
  try(PrintWriter o=new PrintWriter(new File(dir,"positions.tsv"),"UTF-8")){o.println("id\tx\ty\tlabel\tkind");for(Node n:g.getNodes())o.println(n.getId()+"\t"+n.x()+"\t"+n.y()+"\t"+n.getLabel()+"\t"+n.getAttribute("kind"));}
  ExportController ec=Lookup.getDefault().lookup(ExportController.class);
  PreviewController preview=Lookup.getDefault().lookup(PreviewController.class);PreviewModel pm=preview.getModel(ws);
  pm.getProperties().putValue(PreviewProperty.SHOW_NODE_LABELS,true);pm.getProperties().putValue(PreviewProperty.NODE_LABEL_FONT,new Font("SansSerif",Font.PLAIN,10));pm.getProperties().putValue(PreviewProperty.NODE_LABEL_PROPORTIONAL_SIZE,false);pm.getProperties().putValue(PreviewProperty.EDGE_THICKNESS,0.7f);pm.getProperties().putValue(PreviewProperty.EDGE_CURVED,false);
  preview.refreshPreview(ws);ec.exportFile(new File(dir,"community_before.svg"));ec.exportFile(new File(dir,"community_before.pdf"));ec.exportFile(new File(dir,"community_before.gexf"));
  System.out.println("Gephi Senate export: "+g.getNodeCount()+" authors, "+g.getEdgeCount()+" weighted projected links.");System.exit(0);
 }
}
