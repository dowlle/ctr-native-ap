#ifndef AP_SOLO_TRACK_VISIBILITY_HPP
#define AP_SOLO_TRACK_VISIBILITY_HPP

// This client-side adapter runs inside the existing isolated Python child.
// Filter before the wrapper reduces verified descriptors to rows without pins.
// The wrapper supplied by the apworld and existing room files stay on disk.
static constexpr char kSoloTrackVisibility[] = R"PY(
import copy
import runpy
import sys

script, hashes = sys.argv[1:3]
sys.argv = [script, *sys.argv[3:]]
hidden_hashes = set(hashes.split(','))
main = runpy.run_path(script)['main']
scope = main.__globals__
descriptor_sources = scope['_descriptor_sources']
apply_tracks = scope['apply_tracks']

def is_hidden(descriptor):
    files = descriptor.get('files') if isinstance(descriptor, dict) else None
    return isinstance(files, list) and any(
        isinstance(pin, dict) and pin.get('role') == 'lev'
        and isinstance(pin.get('sha256'), str) and pin['sha256'] in hidden_hashes
        for pin in files
    )

def visible_sources(paths):
    return [(d, source) for d, source in descriptor_sources(paths) if not is_hidden(d)]

def yaml_names(descriptor, descriptors):
    # Newer wrappers resolve aliases with the same precedence as generation.
    resolve = scope.get('_yaml_pick_names')
    if resolve:
        return resolve(descriptor, descriptors, [descriptor])
    return {name for name in (descriptor.get('content_id'), descriptor.get('display_name'))
            if isinstance(name, str)}

def enabled_names(setting, names):
    if isinstance(setting, str):
        setting = [name.strip() for name in setting.split(',')]
    if not isinstance(setting, (list, tuple, set, frozenset)) or 'all' in setting:
        return names
    return {name for name in setting if isinstance(name, str) and name in names}

def apply_visible_tracks(paths, doc, assets, picked, guaranteed, page=False):
    section = scope['_section'](doc)
    descriptors = section.get('custom_tracks')
    if isinstance(descriptors, list):
        removed = [d for d in descriptors if is_hidden(d)]
        if removed:
            kept = [d for d in descriptors if not is_hidden(d)]
            plan = scope['_world_module']('content_plan')
            retail = set(plan.RETAIL.values())
            required_names = enabled_names(section.get('retail_tracks'), retail)
            if section.get('crystal_challenge_count') not in (0, '0'):
                arenas = set(plan.arena_content.ARENAS.values())
                required_names = required_names | enabled_names(section.get('crystal_arenas'), arenas)
            mode_names = {'default'} | retail
            if scope['_cortex_pick'](section):
                mode_names.add('Cortex Vortex')
            shared_names = set()
            for d in kept:
                if isinstance(d, dict):
                    shared_names.update(yaml_names(d, descriptors))
            names = set().union(*(yaml_names(d, descriptors) for d in removed)) - shared_names
            removed_required = names - required_names
            doc = copy.deepcopy(doc)
            section = scope['_section'](doc)
            if kept:
                section['custom_tracks'] = copy.deepcopy(kept)
            else:
                section.pop('custom_tracks', None)
            if isinstance(section.get('required_entries'), list):
                section['required_entries'] = [
                    name for name in section['required_entries']
                    if not isinstance(name, str) or name not in removed_required
                ]
            if isinstance(section.get('track_modes'), dict):
                for name in names - mode_names:
                    section['track_modes'].pop(name, None)
    return apply_tracks(paths, doc, assets, picked, guaranteed, page)

scope['_descriptor_sources'] = visible_sources
scope['apply_tracks'] = apply_visible_tracks
sys.exit(main())
)PY";

#endif
