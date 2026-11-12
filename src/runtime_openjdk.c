#include "javaswitch/runtime.h"
#include "javaswitch/native_resolver.h"
#include "runtime_diagnostics.h"
#include <jni.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern void sc_diag_stage(unsigned) __attribute__((weak));
static void runtime_diag_stage(unsigned stage) {
    if (sc_diag_stage != NULL) sc_diag_stage(stage);
}
/* A weak import makes absence explicit. A statically linked VM can supply this
 * standard invocation symbol; no Linux shared object is loaded on Horizon. */
extern jint JNI_CreateJavaVM(JavaVM **,void **,void *) __attribute__((weak));
int js_openjdk_available(void){return JNI_CreateJavaVM!=NULL;}
int js_openjdk_launch(const JsLaunchRequest *q,JsLaunchResult *out) {
    return js_runtime_launch_with_options(q,NULL,0,out);
}

static void log_property(JNIEnv *env, jclass system, jmethodID get_property,
                         const char *name) {
    if((*env)->ExceptionCheck(env))return;
    jstring key=(*env)->NewStringUTF(env,name);
    if(!key)return;
    jstring value=(jstring)(*env)->CallStaticObjectMethod(env,system,get_property,key);
    (*env)->DeleteLocalRef(env,key);
    if(!value||(*env)->ExceptionCheck(env)) {
        fprintf(stderr,"Java bootstrap property %s unavailable\n",name);
        if((*env)->ExceptionCheck(env))(*env)->ExceptionClear(env);
        return;
    }
    const char *utf=(*env)->GetStringUTFChars(env,value,NULL);
    if(utf) {
        fprintf(stderr,"Java bootstrap property %s=%s\n",name,utf);
        (*env)->ReleaseStringUTFChars(env,value,utf);
    }
    (*env)->DeleteLocalRef(env,value);
}

static void diagnose_app_class_resource(JNIEnv *env,const char *main_class) {
    if((*env)->ExceptionCheck(env))return;
    size_t length=strlen(main_class);
    char *resource_name=malloc(length+7);
    if(!resource_name)return;
    memcpy(resource_name,main_class,length);
    for(size_t i=0;i<length;i++)if(resource_name[i]=='.')resource_name[i]='/';
    memcpy(resource_name+length,".class",7);
    jstring name=(*env)->NewStringUTF(env,resource_name);
    free(resource_name);
    if(!name)return;
    jclass loader=(*env)->FindClass(env,"java/lang/ClassLoader");
    jmethodID get_resource=loader?(*env)->GetStaticMethodID(env,loader,
        "getSystemResource","(Ljava/lang/String;)Ljava/net/URL;"):NULL;
    jobject url=get_resource?(*env)->CallStaticObjectMethod(env,loader,get_resource,name):NULL;
    (*env)->DeleteLocalRef(env,name);
    if((*env)->ExceptionCheck(env)) {
        fprintf(stderr,"Java system class-loader resource lookup failed for %s\n",main_class);
        (*env)->ExceptionClear(env);
    } else if(url==NULL) {
        fprintf(stderr,"Java system class-loader resource missing for %s\n",main_class);
    } else {
        jclass url_class=(*env)->GetObjectClass(env,url);
        jmethodID external=(*env)->GetMethodID(env,url_class,"toExternalForm","()Ljava/lang/String;");
        jstring value=external?(jstring)(*env)->CallObjectMethod(env,url,external):NULL;
        const char *utf=value?(*env)->GetStringUTFChars(env,value,NULL):NULL;
        fprintf(stderr,"Java system class-loader resource %s=%s\n",main_class,
                utf?utf:"(URL available)");
        if(utf)(*env)->ReleaseStringUTFChars(env,value,utf);
        if(value)(*env)->DeleteLocalRef(env,value);
        (*env)->DeleteLocalRef(env,url);
    }
    if(loader)(*env)->DeleteLocalRef(env,loader);
}

int js_openjdk_launch_with_options(const JsLaunchRequest *q,const char *const *extra,size_t count,JsLaunchResult *out) {
    runtime_diag_stage(JS_DIAG_SDK_LAUNCH_ENTRY);
    static int created;
    if(created){out->code=JS_UNAVAILABLE;out->stage="JVM-reinitialization-unsupported";return out->code;}
    if(!js_openjdk_available()){out->code=JS_UNAVAILABLE;out->stage="runtime-not-linked";return out->code;}
    size_t bytes=strlen(q->classpath)+32;char *cp=malloc(bytes);
    if(!cp){out->code=JS_UNAVAILABLE;out->stage="native-allocation";return out->code;}
    snprintf(cp,bytes,"-Djava.class.path=%s",q->classpath);char heap[32];snprintf(heap,sizeof(heap),"-Xmx%uM",q->heap_mib);
    JavaVMOption *options=calloc(3+count,sizeof(*options));
    if(!options){free(cp);out->code=JS_UNAVAILABLE;out->stage="native-allocation";return out->code;}
    options[0].optionString=cp;options[1].optionString=heap;options[2].optionString="--enable-native-access=ALL-UNNAMED";
    for(size_t i=0;i<count;i++)options[i+3].optionString=(char *)extra[i];
    JavaVMInitArgs init={.version=JNI_VERSION_1_8,.nOptions=(jint)(3+count),.options=options,.ignoreUnrecognized=JNI_FALSE};
    JavaVM *vm=NULL;JNIEnv *env=NULL;out->stage="JNI_CreateJavaVM";
    js_native_resolver_seal();
    runtime_diag_stage(JS_DIAG_JNI_CREATE_ENTRY);
    jint rc=JNI_CreateJavaVM(&vm,(void **)&env,&init);
    runtime_diag_stage(JS_DIAG_JNI_CREATE_RETURN);
    free(options);free(cp);
    if(rc!=JNI_OK){out->code=JS_UNAVAILABLE;return out->code;}
    created=1;
    jclass bootstrap_system=(*env)->FindClass(env,"java/lang/System");
    jmethodID bootstrap_get_property=bootstrap_system?(*env)->GetStaticMethodID(
        env,bootstrap_system,"getProperty","(Ljava/lang/String;)Ljava/lang/String;"):NULL;
    if(bootstrap_get_property) {
        log_property(env,bootstrap_system,bootstrap_get_property,"java.class.path");
        log_property(env,bootstrap_system,bootstrap_get_property,"path.separator");
        log_property(env,bootstrap_system,bootstrap_get_property,"file.separator");
    } else if((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
    }
    diagnose_app_class_resource(env,q->main_class);
    if(bootstrap_system)(*env)->DeleteLocalRef(env,bootstrap_system);
    if((*env)->ExceptionCheck(env)) {
        fprintf(stderr,"Java bootstrap diagnostics unavailable: diagnostic exception pending\n");
        (*env)->ExceptionClear(env);
    }
    runtime_diag_stage(JS_DIAG_VM_READY);
    int result=JS_JAVA_EXCEPTION;out->stage="vm-ready-hook";
    jclass system=(*env)->FindClass(env,"java/lang/System");
    if(!system)goto cleanup;
    jmethodID get_property=(*env)->GetStaticMethodID(env,system,"getProperty","(Ljava/lang/String;)Ljava/lang/String;");
    if(!get_property)goto cleanup;
    jstring key=(*env)->NewStringUTF(env,"java.specification.version");if(!key)goto cleanup;
    jstring value=(jstring)(*env)->CallStaticObjectMethod(env,system,get_property,key);if(!value||(*env)->ExceptionCheck(env))goto cleanup;
    const char *version=(*env)->GetStringUTFChars(env,value,NULL);if(!version)goto cleanup;
    int actual=atoi(version);(*env)->ReleaseStringUTFChars(env,value,version);
    if(actual!=25||actual<(int)q->required_java_major){result=JS_INCOMPATIBLE;out->stage="actual-java-version-mismatch";goto cleanup;}
    if(q->on_vm_ready&&q->on_vm_ready(vm,env,q->context)!=0)goto cleanup;
    out->stage="main-class";jclass main=(*env)->FindClass(env,q->main_class);
    if(!main||(*env)->ExceptionCheck(env))goto cleanup;
    jmethodID method=(*env)->GetStaticMethodID(env,main,"main","([Ljava/lang/String;)V");
    if(!method||(*env)->ExceptionCheck(env))goto cleanup;
    jclass string=(*env)->FindClass(env,"java/lang/String");if(!string)goto cleanup;
    jobjectArray args=(*env)->NewObjectArray(env,(jsize)q->argument_count,string,NULL);if(!args)goto cleanup;
    for(size_t i=0;i<q->argument_count;i++) {
        jstring value=(*env)->NewStringUTF(env,q->arguments[i]);if(!value)goto cleanup;
        (*env)->SetObjectArrayElement(env,args,(jsize)i,value);(*env)->DeleteLocalRef(env,value);
        if((*env)->ExceptionCheck(env))goto cleanup;
    }
    out->stage="main-invocation";(*env)->CallStaticVoidMethod(env,main,method,args);
    if(!(*env)->ExceptionCheck(env)){result=JS_OK;out->stage="completed";}
cleanup:
    if((*env)->ExceptionCheck(env)){(*env)->ExceptionDescribe(env);(*env)->ExceptionClear(env);}
    rc=(*vm)->DestroyJavaVM(vm);
    if(rc!=JNI_OK&&result==JS_OK){result=JS_JAVA_EXCEPTION;out->stage="DestroyJavaVM";}
    out->code=result;return result;
}
