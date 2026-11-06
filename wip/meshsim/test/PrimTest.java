/** WORKER29 (A): Prim.IntLongMap and Prim.LongRing against HashMap and ArrayDeque, random operations. */
public class PrimTest { public static void main(String[] a) {
  java.util.Random r = new java.util.Random(1); Prim.IntLongMap m = new Prim.IntLongMap(); java.util.HashMap<Integer,Long> h = new java.util.HashMap<>();
  for (int it=0; it<3_000_000; it++) { int key = r.nextInt(it%3==0?50:5000); int op=r.nextInt(3); long val=r.nextLong()&Long.MAX_VALUE;
    if(op==0){m.put(key,val);h.put(key,val);} else if(op==1){Long e=h.get(key); long g=m.get(key); if((e==null?Prim.IntLongMap.ABSENT:e)!=g) throw new RuntimeException("get "+it);} else {Long e=h.remove(key); long g=m.remove(key); if((e==null?Prim.IntLongMap.ABSENT:e)!=g) throw new RuntimeException("rm "+it);}
    if (m.size()!=h.size()) throw new RuntimeException("size "+it); }
  Prim.LongRing q=new Prim.LongRing(); java.util.ArrayDeque<Long> d=new java.util.ArrayDeque<>();
  for(int it=0;it<1_000_000;it++){ if(r.nextInt(3)>0){long x=r.nextLong(); q.add(x); d.add(x);} else if(!d.isEmpty()){ if(d.poll()!=q.poll()) throw new RuntimeException("q");} if(q.size()!=d.size()) throw new RuntimeException("qs"); }
  System.out.println("PrimTest OK"); } }
