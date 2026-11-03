package org.javaswitch.test;
import java.lang.foreign.*;
import java.lang.invoke.*;
import java.nio.file.*;
import java.net.*;
import java.nio.channels.Selector;
public final class Java25Smoke {
    private static native int nativeAdd(int a,int b);
    public record Point(int x,int y) {}
    public static int twice(int value){return value*2;}
    public static double mixed(long left,double value,int scale,float tail){return left+value*scale+tail;}
    static void check(boolean value,String text){if(!value)throw new AssertionError(text);System.out.println("PASS: "+text);}
    public static void main(String[] args)throws Throwable {
        check(Runtime.version().feature()==25,"Java 25 runtime / classfile 69");
        check("yes".equals(System.getProperty("js.option.forwarded")),"structured JVM option forwarding");
        Point p=new Point(20,22);check(p.x()+p.y()==42&&Point.class.getRecordComponents().length==2,"record / reflection");
        check(nativeAdd(20,22)==42,"standard JNI RegisterNatives / native call");
        int[] state={0};Thread t=Thread.ofPlatform().start(()->state[0]=42);t.join();check(state[0]==42,"thread / lambda / synchronization");
        Path file=Files.createTempFile(Path.of(args[0]),"java25-",".txt");
        try{Files.writeString(file,"Horizon Java 25");check(Files.readString(file).equals("Horizon Java 25"),"NIO file roundtrip");}finally{Files.delete(file);}
        InetAddress loopback=InetAddress.getByAddress(new byte[]{127,0,0,1});
        try(ServerSocket server=new ServerSocket()){
            server.setSoTimeout(2000);server.bind(new InetSocketAddress(loopback,0));
            try(Socket client=new Socket()){
                client.connect(server.getLocalSocketAddress(),2000);
                try(Socket peer=server.accept()){
                    peer.setSoTimeout(2000);client.getOutputStream().write(42);
                    check(peer.getInputStream().read()==42,"TCP loopback roundtrip");
                }
            }
        }
        try(Selector selector=Selector.open()){
            long before=System.nanoTime();selector.wakeup();
            check(selector.select(10000)==0&&System.nanoTime()-before<5_000_000_000L,
                  "NIO selector wakeup");
        }
        Linker linker=Linker.nativeLinker();
        try(Arena arena=Arena.ofConfined()){
            MemorySegment text=arena.allocateFrom("javaswitch");
            MethodHandle strlen=linker.downcallHandle(linker.defaultLookup().find("strlen").orElseThrow(),FunctionDescriptor.of(ValueLayout.JAVA_LONG,ValueLayout.ADDRESS));
            check((long)strlen.invokeExact(text)==10,"FFM native downcall");
            MethodHandle pow=linker.downcallHandle(linker.defaultLookup().find("pow").orElseThrow(),
                    FunctionDescriptor.of(ValueLayout.JAVA_DOUBLE,ValueLayout.JAVA_DOUBLE,ValueLayout.JAVA_DOUBLE));
            check((double)pow.invokeExact(2.0,5.0)==32.0,"FFM floating-point libm downcall");
            MethodHandle twice=MethodHandles.lookup().findStatic(Java25Smoke.class,"twice",MethodType.methodType(int.class,int.class));
            MemorySegment up=linker.upcallStub(twice,FunctionDescriptor.of(ValueLayout.JAVA_INT,ValueLayout.JAVA_INT),arena);
            MethodHandle invoke=linker.downcallHandle(up,FunctionDescriptor.of(ValueLayout.JAVA_INT,ValueLayout.JAVA_INT));
            check((int)invoke.invokeExact(21)==42,"FFM upcall / executable trampoline");
            FunctionDescriptor mixedDescriptor=FunctionDescriptor.of(ValueLayout.JAVA_DOUBLE,
                    ValueLayout.JAVA_LONG,ValueLayout.JAVA_DOUBLE,ValueLayout.JAVA_INT,ValueLayout.JAVA_FLOAT);
            MethodHandle mixed=MethodHandles.lookup().findStatic(Java25Smoke.class,"mixed",
                    MethodType.methodType(double.class,long.class,double.class,int.class,float.class));
            MemorySegment mixedUp=linker.upcallStub(mixed,mixedDescriptor,arena);
            MethodHandle invokeMixed=linker.downcallHandle(mixedUp,mixedDescriptor);
            check((double)invokeMixed.invokeExact(7L,2.5,3,0.25f)==14.75,
                    "FFM mixed integer/FP downcall and upcall");
        }
        System.out.println("JAVA25 JNI FFM SUITE PASS");
    }
}
