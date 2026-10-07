# DNnI model backend

Metamorph CR can now select and persist a `.dnni` vocal model.

## Uploaded model used for validation

- File name: `model.dnni`
- Size: `87,529,366` bytes
- SHA-256: `48fe10df60bb4d92d2a5f19f02b4d712dc070bebba9d5ea1ef2172d9f82a428c`
- Container header begins with: `FF 00 CA 7F 02 00 00 00`

The model itself is intentionally **not committed** to this repository. Put your licensed copy somewhere on disk and choose **LOAD DNNI MODEL** in the plugin.

## Important runtime requirement

The `.dnni` file is a proprietary Dreamtonics neural-network container. It is not ONNX, TorchScript, or another public graph format, and this repository does not decrypt or reverse-engineer it.

Actual neural inference therefore requires a legitimate runtime adapter built against an authorized DNNI runtime/API. The plugin looks for a bridge named:

`MetamorphDnniBridge.dll`

beside the standalone executable, or at the path specified by the environment variable:

`METAMORPH_DNNI_BRIDGE`

Without the bridge, the UI reports that the DNnI model is loaded but the plugin continues using its Reference Match fallback.

## Bridge ABI

A compatible bridge exposes this small C ABI:

```c
void* metamorph_dnni_create(
    const char* model_path_utf8,
    double sample_rate,
    int maximum_block_size,
    int channels);

int metamorph_dnni_process(
    void* session,
    float** channel_data,
    int channels,
    int samples);

void metamorph_dnni_reset(void* session);          // optional
void metamorph_dnni_destroy(void* session);
const char* metamorph_dnni_last_error(void* session); // optional
```

`metamorph_dnni_process` returns 0 on success. When the bridge is active, the plugin routes the wet vocal through the DNnI backend and does not apply the spectral Reference Match conversion on top of it.

This adapter boundary is intentionally vendor-neutral: it lets an authorized DNNI SDK/runtime be connected later without embedding proprietary runtime code or bypassing model protections.
