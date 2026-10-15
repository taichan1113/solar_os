#include "solar_os_type_mouse_engine.h"

#include <new>
#include <cstring>
#include "PaperMonoTouchpad.h"

// Generated from the pinned WriterDeck ZMK keymap; the engine keeps its
// physical geometry and layer actions independent of the SolarOS display API.
enum class TypeActionKind : uint8_t { Key, Modifier, Layer, Transparent, Disabled };
struct TypeAction {
    const char *label;
    uint8_t usage, modifiers;
    TypeActionKind kind;
    uint8_t layer;
};
struct TypeKey {
    int logicalPosition;
    const char *label;
    uint8_t usage, modifier;
    int x, y, width, height;
};
struct TypeJpMapping {
    uint8_t sourceUsage, sourceModifiers, outputUsage, outputModifiers;
};
#include "PaperMonoTypeLayout.generated.h"

static constexpr int KEY_X = 18, KEY_Y = 142;
static constexpr int KEY_WIDTH = 764, KEY_HEIGHT = 272;
static constexpr uint32_t HOLD_MS = 550;

struct TypeContact {
    bool active = false, hold = false, used = false;
    uint8_t position = 0;
    uint32_t pressed_ms = 0;
    TypeAction action = {"", 0, 0, TypeActionKind::Disabled, 0};
};
struct MouseSink {
    solar_os_type_mouse_send_t callback = nullptr;
    void *context = nullptr;
    bool send_report(const papermono::PadReport &r) const {
        if (!callback) return false;
        const solar_os_type_mouse_mouse_t report = {
            r.buttons, (int16_t)r.dx, (int16_t)r.dy,
            (int16_t)r.wheel, (int16_t)r.pan};
        return callback(context, &report);
    }
    bool send(const papermono::PadReport &r) const { return send_report(r); }
};

struct solar_os_type_mouse_engine {
    TypeContact contacts[16]{};
    MouseSink sink;
    papermono::Touchpad<MouseSink> *pad = nullptr;
    explicit solar_os_type_mouse_engine(solar_os_type_mouse_send_t callback, void *ctx)
        : sink{callback, ctx} {
        papermono::PadConfig config{};
        config.width = 800; config.height = 480;
        config.inertiaHalfLifeMs = 0;
        pad = new (std::nothrow) papermono::Touchpad<MouseSink>(sink, config);
    }
};

extern "C" int solar_os_type_mouse_engine_hit(int x, int y)
{
    for (const TypeKey &key : kTypeKeys) {
        int left, top, width, height;
        solar_os_type_mouse_engine_rect(key.logicalPosition,
            &left, &top, &width, &height);
        if (x >= left && x < left+width && y >= top && y < top+height)
            return key.logicalPosition;
    }
    return -1;
}

extern "C" bool solar_os_type_mouse_engine_rect(int position, int *x, int *y,
    int *width, int *height)
{
    if (position < 0 || position >= 42 || !x || !y || !width || !height)
        return false;
    const TypeKey &key = kTypeKeys[position];
    *x = KEY_X + (key.x-kTypePhysicalMinX)*KEY_WIDTH/kTypePhysicalWidth;
    *y = KEY_Y + (key.y-kTypePhysicalMinY)*KEY_HEIGHT/kTypePhysicalHeight;
    *width = key.width*KEY_WIDTH/kTypePhysicalWidth-2;
    *height = key.height*KEY_HEIGHT/kTypePhysicalHeight-2;
    return true;
}

extern "C" const char *solar_os_type_mouse_engine_label(int position)
{ return position >= 0 && position < 42 ? kTypeKeys[position].label : ""; }

static uint8_t active_layers(const solar_os_type_mouse_engine_t *engine)
{
    uint8_t layers = 0;
    for (const TypeContact &c : engine->contacts)
        if (c.active && c.hold && kTypeHoldActions[c.position].kind == TypeActionKind::Layer)
            layers |= (uint8_t)(1U << kTypeHoldActions[c.position].layer);
    return layers;
}

static TypeAction action_for(int position, uint8_t layers)
{
    for (int layer = 3; layer >= 1; --layer)
        if (layers & (1U << layer)) {
            const TypeAction a = kTypeLayerActions[layer-1][position];
            if (a.kind != TypeActionKind::Transparent) return a;
        }
    const TypeKey &key = kTypeKeys[position];
    return {key.label, key.usage, key.modifier,
        key.usage ? TypeActionKind::Key : key.modifier ? TypeActionKind::Modifier :
        TypeActionKind::Disabled, 0};
}

extern "C" uint8_t solar_os_type_mouse_engine_layer(
    const solar_os_type_mouse_engine_t *engine)
{
    const uint8_t layers = engine ? active_layers(engine) : 0;
    for (int layer = 3; layer >= 1; --layer)
        if (layers & (1U << layer)) return (uint8_t)layer;
    return 0;
}

extern "C" const char *solar_os_type_mouse_engine_layer_label(
    const solar_os_type_mouse_engine_t *engine)
{
    const uint8_t layer = solar_os_type_mouse_engine_layer(engine);
    if (!layer) return "Base";
    for (const TypeAction &action : kTypeHoldActions)
        if (action.kind == TypeActionKind::Layer && action.layer == layer)
            return action.label;
    return "Base";
}

extern "C" const char *solar_os_type_mouse_engine_tap_label(
    const solar_os_type_mouse_engine_t *engine, int position)
{
    if (position < 0 || position >= 42) return "";
    const uint8_t layers = engine ? active_layers(engine) : 0;
    const TypeAction action = action_for(position, layers);
    if (position == 41) return kTypeKeys[position].label;
    return action.kind == TypeActionKind::Disabled ? "" : action.label;
}

extern "C" const char *solar_os_type_mouse_engine_hold_label(int position)
{
    if (position < 0 || position >= 42) return "";
    const TypeAction &action = kTypeHoldActions[position];
    return action.kind == TypeActionKind::Layer ||
        action.kind == TypeActionKind::Modifier ? action.label : "";
}

extern "C" uint8_t solar_os_type_mouse_engine_held_modifiers(
    const solar_os_type_mouse_engine_t *engine)
{
    if (!engine) return 0;
    uint8_t modifiers = 0;
    for (const TypeContact &contact : engine->contacts) {
        if (!contact.active) continue;
        if (contact.hold &&
            kTypeHoldActions[contact.position].kind == TypeActionKind::Modifier)
            modifiers |= kTypeHoldActions[contact.position].modifiers;
        else if (!contact.hold && contact.action.kind == TypeActionKind::Modifier)
            modifiers |= contact.action.modifiers;
    }
    return modifiers;
}

static void jp_map(uint8_t &usage, uint8_t &modifiers)
{
    for (const TypeJpMapping &mapping : kTypeJpMappings)
        if (usage == mapping.sourceUsage &&
            (modifiers & 0x02U) == mapping.sourceModifiers) {
            usage = mapping.outputUsage;
            modifiers = (uint8_t)((modifiers & ~0x02U) | mapping.outputModifiers);
            break;
        }
}

extern "C" solar_os_type_mouse_engine_t *solar_os_type_mouse_engine_create(
    solar_os_type_mouse_send_t send, void *context)
{
    if (!send) return nullptr;
    solar_os_type_mouse_engine_t *engine = new (std::nothrow)
        solar_os_type_mouse_engine_t(send, context);
    if (engine && !engine->pad) { delete engine; return nullptr; }
    return engine;
}
extern "C" void solar_os_type_mouse_engine_destroy(solar_os_type_mouse_engine_t *engine)
{ if (engine) { delete engine->pad; delete engine; } }
extern "C" void solar_os_type_mouse_engine_cancel(solar_os_type_mouse_engine_t *engine)
{
    if (!engine) return;
    for (TypeContact &c : engine->contacts) c = TypeContact{};
    engine->pad->cancel();
}

extern "C" void solar_os_type_mouse_engine_type(solar_os_type_mouse_engine_t *engine,
    const solar_os_type_mouse_point_t *points, size_t count, uint32_t ms,
    solar_os_type_mouse_keys_t *out)
{
    if (!engine || !out || count > 2 || (count && !points)) return;
    *out = solar_os_type_mouse_keys_t{};
    bool seen[16]{};
    for (size_t i = 0; i < count; ++i) if (points[i].id < 16) seen[points[i].id] = true;
    for (TypeContact &c : engine->contacts) {
        if (!c.active || seen[&c-engine->contacts]) continue;
        if (c.hold && !c.used && ms-c.pressed_ms < HOLD_MS) {
            if (c.position == 41) out->switch_mouse = true;
            else if (c.action.kind == TypeActionKind::Key) {
                out->tap_usage = c.action.usage;
                out->tap_modifiers = c.action.modifiers;
            }
        }
        c = TypeContact{};
    }
    for (size_t i = 0; i < count; ++i) {
        const uint8_t id = points[i].id;
        if (id >= 16 || engine->contacts[id].active) continue;
        const int position = solar_os_type_mouse_engine_hit(points[i].x, points[i].y);
        if (position < 0) continue;
        for (TypeContact &other : engine->contacts)
            if (other.active && other.hold) other.used = true;
        TypeContact &c = engine->contacts[id];
        c.active = true;
        c.position = (uint8_t)position;
        c.action = action_for(position, active_layers(engine));
        const TypeAction hold = kTypeHoldActions[position];
        c.hold = hold.kind == TypeActionKind::Layer ||
            hold.kind == TypeActionKind::Modifier || position == 41;
        c.pressed_ms = ms;
    }
    uint8_t count_keys = 0;
    for (const TypeContact &c : engine->contacts) {
        if (!c.active) continue;
        if (c.hold) {
            const TypeAction &hold = kTypeHoldActions[c.position];
            if (hold.kind == TypeActionKind::Modifier) out->modifiers |= hold.modifiers;
        } else out->modifiers |= c.action.modifiers;
    }
    for (const TypeContact &c : engine->contacts) {
        if (!c.active || c.hold || c.action.kind != TypeActionKind::Key) continue;
        uint8_t usage = c.action.usage, modifiers = out->modifiers;
        jp_map(usage, modifiers);
        bool duplicate = false;
        for (uint8_t i = 0; i < count_keys; ++i) duplicate |= out->keys[i] == usage;
        if (!duplicate && count_keys < 6) {
            out->keys[count_keys++] = usage;
            out->modifiers = modifiers;
        }
    }
    if (out->tap_usage) {
        out->tap_modifiers |= out->modifiers;
        jp_map(out->tap_usage, out->tap_modifiers);
    }
}

extern "C" void solar_os_type_mouse_engine_mouse(solar_os_type_mouse_engine_t *engine,
    const solar_os_type_mouse_point_t *points, size_t count,
    uint32_t sequence, uint32_t ms)
{
    if (!engine || !engine->pad || count > 2 || (count && !points)) return;
    papermono::PadFrame frame{};
    frame.sequence = sequence; frame.ms = ms; frame.count = (uint8_t)count;
    for (size_t i = 0; i < count; ++i)
        frame.points[i] = {(int32_t)points[i].id, points[i].x, points[i].y};
    engine->pad->process(frame);
}
