from setuptools import setup, Extension

module = Extension('directory_monitor', sources=['src/listener_dir.c'], extra_link_args=['-framework', 'CoreServices'])

setup(
    name='directory_monitor',
    version='1.0',
    description='Directory monitoring module',
    ext_modules=[module]
)
