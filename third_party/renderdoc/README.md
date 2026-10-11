# RenderDoc application API

`renderdoc_app.h` is the unmodified application API header from RenderDoc **v1.35**:
[upstream source](https://github.com/baldurk/renderdoc/blob/v1.35/renderdoc/api/app/renderdoc_app.h).
Its MIT license is included at the top of the header. Spot requests API 1.6.0 and loads RenderDoc optionally;
games do not link or distribute the RenderDoc runtime. Backend types stay private to the platform implementation.
