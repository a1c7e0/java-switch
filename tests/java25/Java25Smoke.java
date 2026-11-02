package org.javaswitch.test;
import java.lang.foreign.*;
import java.lang.invoke.*;
import java.nio.file.*;
public final class Java25Smoke {
    private static native int nativeAdd(int a,int b);
    public record Point(int x,int y) {}
    public static int twice(int value){return value*2;}
    static void check(boolean value,String text){if(!value)throw new AssertionError(text);System.out.println("PASS: "+text);}
    public static void main(String[] args)throws Throwable {
        check(Runtime.version().feature()==25,"Java 25 runtime / classfile 69");
        Point p=new Point(20,22);check(p.x()+p.y()==42&&Point.class.getRecordComponents().length==2,"record / reflection");
        check(nativeAdd(20,22)==42,"standard JNI RegisterNatives / native call");
        int[] state={0};Thread t=Thread.ofPlatform().start(()->state[0]=42);t.join();check(state[0]==42,"thread / lambda / synchronization");
        Path file=Files.createTempFile(Path.of(args[0]),"java25-",".txt");
        try{Files.writeString(file,"Horizon Java 25");check(Files.readString(file).equals("Horizon Java 25"),"NIO file roundtrip");}finally{Files.delete(file);}
        Linker linker=Linker.nativeLinker();
        try(Arena arena=Arena.ofConfined()){
            MemorySegment text=arena.allocateFrom("javaswitch");
            MethodHandle strlen=linker.downcallHandle(linker.defaultLookup().find("strlen").orElseThrow(),FunctionDescriptor.of(ValueLayout.JAVA_LONG,ValueLayout.ADDRESS));
            check((long)strlen.invokeExact(text)==10,"FFM native downcall");
            MethodHandle twice=MethodHandles.lookup().findStatic(Java25Smoke.class,"twice",MethodType.methodType(int.class,int.class));
            MemorySegment up=linker.upcallStub(twice,FunctionDescriptor.of(ValueLayout.JAVA_INT,ValueLayout.JAVA_INT),arena);
            MethodHandle invoke=linker.downcallHandle(up,FunctionDescriptor.of(ValueLayout.JAVA_INT,ValueLayout.JAVA_INT));
            check((int)invoke.invokeExact(21)==42,"FFM upcall / executable trampoline");
        }
        System.out.println("JAVA25 JNI FFM SUITE PASS");
    }
}
