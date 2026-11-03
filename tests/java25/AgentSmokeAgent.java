import java.lang.instrument.ClassFileTransformer;
import java.lang.instrument.Instrumentation;
import java.lang.instrument.IllegalClassFormatException;
import java.security.ProtectionDomain;
import java.lang.classfile.ClassFile;
import java.lang.classfile.ClassTransform;
import java.lang.classfile.instruction.ConstantInstruction;
import java.util.concurrent.atomic.AtomicInteger;

public final class AgentSmokeAgent {
    private static final String TARGET = "AgentSmoke";
    private static final String BEFORE = "sentinel-before-agent";
    private static final String AFTER = "sentinel-transformed-before-define";
    private static final AtomicInteger PREMAIN_INVOCATIONS = new AtomicInteger();
    private static final AtomicInteger TRANSFORM_INVOCATIONS = new AtomicInteger();

    public static void premain(String agentArgs, Instrumentation instrumentation) {
        if (!"premain-argument-forwarded".equals(agentArgs)) {
            throw new IllegalArgumentException("unexpected premain argument: " + agentArgs);
        }
        System.setProperty("agent.smoke.argument", agentArgs);
        System.setProperty("agent.smoke.premain.count",
                Integer.toString(PREMAIN_INVOCATIONS.incrementAndGet()));
        System.setProperty("agent.smoke.instrumentation.redefine",
                Boolean.toString(instrumentation.isRedefineClassesSupported()));
        System.setProperty("agent.smoke.instrumentation.retransform",
                Boolean.toString(instrumentation.isRetransformClassesSupported()));
        System.setProperty("agent.smoke.instrumentation.nativePrefix",
                Boolean.toString(instrumentation.isNativeMethodPrefixSupported()));

        instrumentation.addTransformer(new ClassFileTransformer() {
            @Override
            public byte[] transform(ClassLoader loader, String className,
                                    Class<?> classBeingRedefined,
                                    ProtectionDomain protectionDomain,
                                    byte[] classfileBuffer) throws IllegalClassFormatException {
                if (!TARGET.equals(className)) return null;
                if (classBeingRedefined != null) {
                    throw new IllegalClassFormatException("sentinel was retransformed");
                }
                var model = ClassFile.of().parse(classfileBuffer);
                byte[] transformed = ClassFile.of().transformClass(model,
                        ClassTransform.transformingMethodBodies(
                                method -> method.methodName().stringValue().equals("sentinel"),
                                (builder, element) -> {
                                    if (element instanceof ConstantInstruction constant
                                            && BEFORE.equals(constant.constantValue())) {
                                        builder.ldc(AFTER);
                                    } else {
                                        builder.accept(element);
                                    }
                                }));
                System.setProperty("agent.smoke.transform.count",
                        Integer.toString(TRANSFORM_INVOCATIONS.incrementAndGet()));
                return transformed;
            }
        }, false);
    }
}
