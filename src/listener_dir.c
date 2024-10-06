#include <Python.h>
#include <CoreServices/CoreServices.h>
#include <pthread.h>

static PyObject *event_callback = NULL;
static PyObject *loop = NULL;
static pthread_t monitor_thread;
static FSEventStreamRef stream = NULL;
static char *directory_path;
static bool running = true;

static const char* get_event_type(FSEventStreamEventFlags flags) {
    if (flags & kFSEventStreamEventFlagItemCreated) {
        return "created";
    } else if (flags & kFSEventStreamEventFlagItemRemoved) {
        return "deleted";
    } else if (flags & kFSEventStreamEventFlagItemModified) {
        return "modified";
    } else if (flags & kFSEventStreamEventFlagItemRenamed) {
        return "renamed";
    } else {
        return "unknown";
    }
}

static void callback(ConstFSEventStreamRef streamRef,
                     void *clientCallBackInfo,
                     size_t numEvents,
                     void *eventPaths,
                     const FSEventStreamEventFlags eventFlags[],
                     const FSEventStreamEventId eventIds[]) {
    size_t i;
    char **paths = eventPaths;
    for (i = 0; i < numEvents; i++) {
        printf("Event detected: %s, Flag: %u\n", paths[i], eventFlags[i]);

        const char *event_type = get_event_type(eventFlags[i]);
        printf("Determined event type: %s\n", event_type);

        if (event_callback) {
            printf("In event callback working\n");
            PyGILState_STATE gstate;
            gstate = PyGILState_Ensure();

            PyObject *arg = Py_BuildValue("(ssi)", paths[i], event_type, eventFlags[i]);
            PyObject *awaitable = PyObject_CallObject(event_callback, arg);
            Py_XDECREF(arg);

            if (awaitable) {
                if (loop) {
                    PyObject *result = PyObject_CallMethod(loop, "create_task", "O", awaitable);
                    Py_XDECREF(result);
                }
            } else {
                PyErr_Print();
            }
            Py_XDECREF(awaitable);
            PyGILState_Release(gstate);
        } else {
            PyErr_Print();
        }
    }
}

static void *monitor_directory(void *arg) {
    FSEventStreamContext context = {0, NULL, NULL, NULL, NULL};
    CFStringRef mypath = CFStringCreateWithCString(NULL, directory_path, kCFStringEncodingUTF8);
    CFArrayRef pathsToWatch = CFArrayCreate(NULL, (const void **)&mypath, 1, NULL);
    CFAbsoluteTime latency = 1.0;

    stream = FSEventStreamCreate(NULL,
                                 (FSEventStreamCallback)&callback,
                                 &context,
                                 pathsToWatch,
                                 kFSEventStreamEventIdSinceNow,
                                 latency,
                                 kFSEventStreamCreateFlagFileEvents);

    FSEventStreamScheduleWithRunLoop(stream, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    FSEventStreamStart(stream);
    CFRelease(pathsToWatch);
    CFRelease(mypath);

    CFRunLoopRun();

    return NULL;
}

static PyObject *set_callback(PyObject *self, PyObject *args) {
    PyObject *temp;
    if (PyArg_ParseTuple(args, "O", &temp)) {
        if (!PyCallable_Check(temp)) {
            PyErr_SetString(PyExc_TypeError, "parameter must be callable");
            return NULL;
        }
        Py_XINCREF(temp);
        Py_XDECREF(event_callback);
        event_callback = temp;
        Py_RETURN_NONE;
    }
    return NULL;
}

static PyObject *start_monitor(PyObject *self, PyObject *args) {
    if (!PyArg_ParseTuple(args, "sO", &directory_path, &loop)) {
        return NULL;
    }

    printf("Monitoring directory: %s\n", directory_path);

    Py_XINCREF(loop);

    int result = pthread_create(&monitor_thread, NULL, monitor_directory, NULL);
    if (result != 0) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to create monitoring thread");
        return NULL;
    }

    Py_RETURN_NONE;
}

static PyObject *stop(PyObject *self, PyObject *args){
    running = false;
    if (stream) {
        CFRunLoopStop(CFRunLoopGetCurrent());
    }
    pthread_join(monitor_thread, NULL);
    Py_XDECREF(loop);
    Py_RETURN_NONE;
}

static PyMethodDef DirectoryMonitorMethods[] = {
    {"set_callback", set_callback, METH_VARARGS, "Set the callback function."},
    {"start_monitor", start_monitor, METH_VARARGS, "Start monitoring a directory."},
    {"stop", stop, METH_NOARGS, "Stop monitoring the directory."},
    {NULL, NULL, 0, NULL}
};

static struct PyModuleDef directorymonitormodule = {
    PyModuleDef_HEAD_INIT,
    "directory_monitor",
    NULL,
    -1,
    DirectoryMonitorMethods
};

PyMODINIT_FUNC PyInit_directory_monitor(void) {
    return PyModule_Create(&directorymonitormodule);
}
