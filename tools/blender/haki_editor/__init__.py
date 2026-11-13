"""Haki Blender extension. Metadata utilities can also be imported outside Blender."""

def register():
    from . import editor
    editor.register()


def unregister():
    from . import editor
    editor.unregister()
