from setuptools import setup

package_name = 'agt_map_refinement_core'

setup(
    name=package_name,
    version='0.3.0',
    packages=[package_name],
    data_files=[
        ('share/ament_index/resource_index/packages', ['resource/' + package_name]),
        ('share/' + package_name, ['package.xml']),
        ('share/' + package_name, ['UPSTREAM_VISIBILITY_LICENSE']),
    ],
    install_requires=['setuptools', 'PyYAML', 'numpy', 'scipy'],
    zip_safe=True,
    maintainer='AGT Mapping Team',
    maintainer_email='',
    description='Offline map refinement rules, package publication and Nav2 derivatives.',
    license='Apache-2.0',
    tests_require=['pytest'],
    entry_points={'console_scripts': [
        'apply_map_refinement = agt_map_refinement_core.cli:main',
        'filter_livo_visibility = agt_map_refinement_core.livo_visibility:main',
    ]},
)
