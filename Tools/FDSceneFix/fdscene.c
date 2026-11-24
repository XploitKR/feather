// FDSceneFix v2: minimal UIWindowSceneDelegate for legacy (AppDelegate-window) apps
// whose SDK field was forced to iOS 27, where UIScene adoption is mandatory.
// Uses only the ObjC runtime (no UIKit headers) so it builds without the iOS SDK.
//
// - Code-created windows: attaches the app delegate's `window` to the scene.
// - Storyboard apps: Info.plist gets UISceneStoryboardFile; UIKit hands the storyboard
//   window to -setWindow:, which is mirrored back to the app delegate's `window`.
// - Secondary windows created with -initWithFrame: (toasts, overlays, SDK popups) are
//   attached to the scene when they are shown (UIWindow -setHidden:/-makeKeyAndVisible hook).
// - Forwards URL opens (with sourceApplication/annotation/openInPlace), user activities,
//   home-screen quick actions and app lifecycle callbacks to the app delegate.
#include <stddef.h>
#include <stdbool.h>

typedef struct objc_object *id;
typedef struct objc_selector *SEL;
typedef struct objc_class *Class;
typedef struct objc_method *Method;
typedef struct objc_protocol Protocol;
typedef void (*IMP)(void);
typedef struct { double x, y, w, h; } CGRect;

extern id objc_getClass(const char *);
extern SEL sel_registerName(const char *);
extern Class objc_allocateClassPair(Class, const char *, size_t);
extern void objc_registerClassPair(Class);
extern bool class_addMethod(Class, SEL, IMP, const char *);
extern Method class_getInstanceMethod(Class, SEL);
extern IMP method_setImplementation(Method, IMP);
extern Protocol *objc_getProtocol(const char *);
extern bool class_addProtocol(Class, Protocol *);
extern void objc_setAssociatedObject(id, const void *, id, unsigned long);
extern id objc_getAssociatedObject(id, const void *);
extern void objc_msgSend(void);
extern void *dlsym(void *, const char *);
#define RTLD_DEFAULT ((void *)-2)

#define S(name) sel_registerName(name)
#define MSG0(r, o, s) ((r (*)(id, SEL))objc_msgSend)((id)(o), S(s))
#define MSG1(r, o, s, a) ((r (*)(id, SEL, id))objc_msgSend)((id)(o), S(s), (id)(a))
#define MSG2(r, o, s, a, b) ((r (*)(id, SEL, id, id))objc_msgSend)((id)(o), S(s), (id)(a), (id)(b))
#define MSG3(r, o, s, a, b, c) ((r (*)(id, SEL, id, id, id))objc_msgSend)((id)(o), S(s), (id)(a), (id)(b), (id)(c))
#define MSG4(r, o, s, a, b, c, d) ((r (*)(id, SEL, id, id, id, id))objc_msgSend)((id)(o), S(s), (id)(a), (id)(b), (id)(c), (id)(d))

static char kWindowKey;
static id gScene;           // the connected window scene (UIKit keeps it alive while connected)
static id gPending;         // NSMutableArray of windows shown before a scene existed
static bool gAdopting;

static bool responds(id obj, const char *sel) {
    if (!obj) return false;
    return ((bool (*)(id, SEL, SEL))objc_msgSend)(obj, S("respondsToSelector:"), S(sel));
}
static id app(void) { return MSG0(id, objc_getClass("UIApplication"), "sharedApplication"); }
static id appDelegate(void) { return MSG0(id, app(), "delegate"); }
static id str(const char *s) { return ((id (*)(id, SEL, const char *))objc_msgSend)(objc_getClass("NSString"), S("stringWithUTF8String:"), s); }
static unsigned long count(id c) { return c ? MSG0(unsigned long, c, "count") : 0; }
static id at(id a, unsigned long i) { return ((id (*)(id, SEL, unsigned long))objc_msgSend)(a, S("objectAtIndex:"), i); }

// UIKit's exported NSString constant, or its well-known value as a fallback.
static id uikitKey(const char *symbol) {
    id *p = (id *)dlsym(RTLD_DEFAULT, symbol);
    return (p && *p) ? *p : str(symbol);
}

#pragma mark - Window adoption

static void attachWindow(id window, bool fitToScene) {
    if (!window || !gScene) return;
    if (MSG0(id, window, "windowScene")) return;
    gAdopting = true;
    MSG1(void, window, "setWindowScene:", gScene);
    gAdopting = false;
    if (fitToScene) {
        id space = MSG0(id, gScene, "coordinateSpace");
        CGRect bounds = ((CGRect (*)(id, SEL))objc_msgSend)(space, S("bounds"));
        ((void (*)(id, SEL, CGRect))objc_msgSend)(window, S("setFrame:"), bounds);
    }
}

static void adopt(id window) {
    if (gAdopting || !window || MSG0(id, window, "windowScene")) return;
    if (gScene) { attachWindow(window, false); return; }
    if (!gPending) gPending = MSG0(id, MSG0(id, objc_getClass("NSMutableArray"), "alloc"), "init");
    if (!((bool (*)(id, SEL, id))objc_msgSend)(gPending, S("containsObject:"), window))
        MSG1(void, gPending, "addObject:", window);
}

static void (*orig_setHidden)(id, SEL, bool);
static void fd_setHidden(id self, SEL _cmd, bool hidden) {
    if (!hidden) adopt(self);
    orig_setHidden(self, _cmd, hidden);
}
static void (*orig_makeKeyAndVisible)(id, SEL);
static void fd_makeKeyAndVisible(id self, SEL _cmd) {
    adopt(self);
    orig_makeKeyAndVisible(self, _cmd);
}

#pragma mark - Scene delegate

static id fd_window(id self, SEL _cmd) { return objc_getAssociatedObject(self, &kWindowKey); }
static void fd_setWindow(id self, SEL _cmd, id w) {
    objc_setAssociatedObject(self, &kWindowKey, w, 1 /* RETAIN_NONATOMIC */);
    // Storyboard window from UISceneStoryboardFile: expose it where legacy code looks.
    id del = appDelegate();
    if (w && responds(del, "window") && !MSG0(id, del, "window") && responds(del, "setWindow:"))
        MSG1(void, del, "setWindow:", w);
}

static id openURLOptions(id ctxOptions) {
    id dict = MSG0(id, objc_getClass("NSMutableDictionary"), "dictionary");
    if (!ctxOptions) return dict;
    id source = responds(ctxOptions, "sourceApplication") ? MSG0(id, ctxOptions, "sourceApplication") : 0;
    id annotation = responds(ctxOptions, "annotation") ? MSG0(id, ctxOptions, "annotation") : 0;
    bool inPlace = responds(ctxOptions, "openInPlace") ? MSG0(bool, ctxOptions, "openInPlace") : false;
    if (source) MSG2(void, dict, "setObject:forKey:", source, uikitKey("UIApplicationOpenURLOptionsSourceApplicationKey"));
    if (annotation) MSG2(void, dict, "setObject:forKey:", annotation, uikitKey("UIApplicationOpenURLOptionsAnnotationKey"));
    id number = ((id (*)(id, SEL, bool))objc_msgSend)(objc_getClass("NSNumber"), S("numberWithBool:"), inPlace);
    MSG2(void, dict, "setObject:forKey:", number, uikitKey("UIApplicationOpenURLOptionsOpenInPlaceKey"));
    return dict;
}

static void forwardURLs(id contexts) {
    id del = appDelegate();
    if (!contexts || !del) return;
    id list = MSG0(id, contexts, "allObjects");
    for (unsigned long i = 0, n = count(list); i < n; ++i) {
        id ctx = at(list, i);
        id url = MSG0(id, ctx, "URL");
        if (!url) continue;
        id opts = MSG0(id, ctx, "options");
        if (responds(del, "application:openURL:options:")) {
            MSG3(bool, del, "application:openURL:options:", app(), url, openURLOptions(opts));
        } else if (responds(del, "application:openURL:sourceApplication:annotation:")) {
            id source = opts && responds(opts, "sourceApplication") ? MSG0(id, opts, "sourceApplication") : 0;
            id annotation = opts && responds(opts, "annotation") ? MSG0(id, opts, "annotation") : 0;
            MSG4(bool, del, "application:openURL:sourceApplication:annotation:", app(), url, source, annotation);
        } else if (responds(del, "application:handleOpenURL:")) {
            MSG2(bool, del, "application:handleOpenURL:", app(), url);
        }
    }
}

static void forwardActivity(id activity) {
    id del = appDelegate();
    if (!activity || !responds(del, "application:continueUserActivity:restorationHandler:")) return;
    void (^handler)(id) = ^(id restorable) { (void)restorable; };
    MSG3(bool, del, "application:continueUserActivity:restorationHandler:", app(), activity, (id)handler);
}

static void forwardShortcut(id item, id completion) {
    id del = appDelegate();
    if (item && responds(del, "application:performActionForShortcutItem:completionHandler:"))
        MSG3(void, del, "application:performActionForShortcutItem:completionHandler:", app(), item, completion);
    else if (completion)
        ((void (^)(bool))completion)(false);
}

static void fd_willConnect(id self, SEL _cmd, id scene, id session, id options) {
    gScene = scene;
    id del = appDelegate();
    id window = fd_window(self, NULL);                       // storyboard window, if any
    if (!window && responds(del, "window")) window = MSG0(id, del, "window");
    if (window) {
        attachWindow(window, true);
        if (!fd_window(self, NULL)) objc_setAssociatedObject(self, &kWindowKey, window, 1);
    }
    for (unsigned long i = 0, n = count(gPending); i < n; ++i) {
        id w = at(gPending, i);
        if (w != window) attachWindow(w, false);
    }
    if (gPending) MSG0(void, gPending, "removeAllObjects");
    if (window) MSG0(void, window, "makeKeyAndVisible");
    if (options) {
        forwardURLs(MSG0(id, options, "URLContexts"));
        id acts = MSG0(id, MSG0(id, options, "userActivities"), "allObjects");
        for (unsigned long i = 0, n = count(acts); i < n; ++i) forwardActivity(at(acts, i));
        if (responds(options, "shortcutItem")) {
            id item = MSG0(id, options, "shortcutItem");
            if (item) forwardShortcut(item, 0);
        }
    }
}

static void fd_didDisconnect(id self, SEL _cmd, id scene) { if (gScene == scene) gScene = 0; }
static void fd_openURLContexts(id self, SEL _cmd, id scene, id contexts) { forwardURLs(contexts); }
static void fd_continueActivity(id self, SEL _cmd, id scene, id activity) { forwardActivity(activity); }
static void fd_shortcut(id self, SEL _cmd, id scene, id item, id completion) { forwardShortcut(item, completion); }

static void forwardLifecycle(const char *sel) {
    id del = appDelegate();
    if (responds(del, sel)) MSG1(void, del, sel, app());
}
static void fd_didBecomeActive(id self, SEL _cmd, id scene) { forwardLifecycle("applicationDidBecomeActive:"); }
static void fd_willResignActive(id self, SEL _cmd, id scene) { forwardLifecycle("applicationWillResignActive:"); }
static void fd_willEnterForeground(id self, SEL _cmd, id scene) { forwardLifecycle("applicationWillEnterForeground:"); }
static void fd_didEnterBackground(id self, SEL _cmd, id scene) { forwardLifecycle("applicationDidEnterBackground:"); }

static void hook(Class cls, const char *sel, IMP replacement, IMP *original) {
    Method m = class_getInstanceMethod(cls, S(sel));
    if (m) *original = method_setImplementation(m, replacement);
}

__attribute__((constructor)) static void fd_register(void) {
    if (objc_getClass("FDSceneDelegate")) return;
    Class cls = objc_allocateClassPair((Class)objc_getClass("UIResponder"), "FDSceneDelegate", 0);
    if (!cls) return;
    class_addMethod(cls, S("window"), (IMP)fd_window, "@@:");
    class_addMethod(cls, S("setWindow:"), (IMP)fd_setWindow, "v@:@");
    class_addMethod(cls, S("scene:willConnectToSession:options:"), (IMP)fd_willConnect, "v@:@@@");
    class_addMethod(cls, S("sceneDidDisconnect:"), (IMP)fd_didDisconnect, "v@:@");
    class_addMethod(cls, S("scene:openURLContexts:"), (IMP)fd_openURLContexts, "v@:@@");
    class_addMethod(cls, S("scene:continueUserActivity:"), (IMP)fd_continueActivity, "v@:@@");
    class_addMethod(cls, S("windowScene:performActionForShortcutItem:completionHandler:"), (IMP)fd_shortcut, "v@:@@@?");
    class_addMethod(cls, S("sceneDidBecomeActive:"), (IMP)fd_didBecomeActive, "v@:@");
    class_addMethod(cls, S("sceneWillResignActive:"), (IMP)fd_willResignActive, "v@:@");
    class_addMethod(cls, S("sceneWillEnterForeground:"), (IMP)fd_willEnterForeground, "v@:@");
    class_addMethod(cls, S("sceneDidEnterBackground:"), (IMP)fd_didEnterBackground, "v@:@");
    Protocol *p = objc_getProtocol("UIWindowSceneDelegate");
    if (p) class_addProtocol(cls, p);
    objc_registerClassPair(cls);

    Class window = (Class)objc_getClass("UIWindow");
    if (window) {
        hook(window, "setHidden:", (IMP)fd_setHidden, (IMP *)&orig_setHidden);
        hook(window, "makeKeyAndVisible", (IMP)fd_makeKeyAndVisible, (IMP *)&orig_makeKeyAndVisible);
    }
}
