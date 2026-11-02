import java.io.*;
import java.lang.reflect.Method;

/** Original diagnostic workload; no Minecraft code or account data. */
public class DiagnosticSmoke {
    private static native int nativeAdd(int a, int b);
    public static int twice(int value) { return value * 2; }
    private static void check(boolean ok, String name) {
        if (!ok) throw new RuntimeException("FAIL: " + name);
        System.out.println("PASS: " + name);
    }
    public static void main(String[] args) throws Exception {
        System.out.println("Hello World from Java bytecode");
        int sum=0; for(int i=1;i<=1000;i++)sum+=i;
        check(sum==500500,"arithmetic");
        int zero=args.length-args.length; boolean caught=false;
        try { int ignored=42/zero; System.out.println(ignored); }
        catch(ArithmeticException expected) {caught=true;}
        check(caught,"exception handling");
        Class<?> extra=Class.forName("JarDependency");
        check(extra.getName().equals("JarDependency"),"separate JAR class loading");
        Method method=DiagnosticSmoke.class.getMethod("twice",new Class[]{Integer.TYPE});
        check(((Integer)method.invoke(null,new Object[]{Integer.valueOf(21)})).intValue()==42,"reflection invocation");
        final int[] result={0};
        Thread worker=new Thread(new Runnable(){public void run(){result[0]=42;}});
        worker.start(); worker.join(); check(result[0]==42,"Java thread start/join");
        byte[][] blocks=new byte[128][];
        for(int i=0;i<blocks.length;i++){blocks[i]=new byte[4096];blocks[i][0]=(byte)i;}
        check(blocks[127][0]==127,"allocation 512 KiB");
        if(args.length>0) {
            File dir=new File(args[0]);dir.mkdirs();
            File file=new File(dir,"java-smoke.txt");
            check(file.createNewFile(),"exclusive file creation");
            try {
                FileOutputStream out=new FileOutputStream(file);out.write(new byte[]{1,2,3,4});out.close();
                FileInputStream in=new FileInputStream(file);
                check(in.read()==1&&in.read()==2&&in.read()==3&&in.read()==4&&in.read()==-1,"Java file roundtrip");in.close();
            }finally {check(file.delete(),"Java test file cleanup");}
        }
        if(args.length>1&&args[1].equals("--native"))check(nativeAdd(20,22)==42,"miniJVM static native bridge (not JNI invocation)");
        System.out.println("DIAGNOSTIC SUITE PASS");
    }
}
