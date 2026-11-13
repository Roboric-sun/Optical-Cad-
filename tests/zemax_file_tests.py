"""Real Zemax 13 files supplied by the user; independently check every surface."""
import hashlib
import json
import math
from pathlib import Path
import sys
import tempfile

binary, root = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
sys.path.insert(0, str(root / 'scripts'))
from opticalcad import Project, OpticalCADError

def prescription(path):
    rows = []
    for line in path.read_text(encoding='utf-16').splitlines():
        parts = line.split()
        if not parts:
            continue
        if parts[0] == 'SURF':
            rows.append({'number': int(parts[1]), 'material': 'AIR'})
        elif rows and parts[0] in ('CURV', 'DISZ', 'DIAM', 'GLAS'):
            rows[-1][parts[0]] = parts[1]
            if parts[0] == 'GLAS':
                rows[-1]['material'] = parts[1]
        elif rows and parts[0] == 'STOP':
            rows[-1]['stop'] = True
    return rows

def close(a, b):
    assert math.isclose(a, b, rel_tol=2e-13, abs_tol=1e-11), (a, b)

report = []
with tempfile.TemporaryDirectory(prefix='zemax-files-') as temporary:
    output = Path(sys.argv[3]) if len(sys.argv) > 3 else Path(temporary)
    output.mkdir(parents=True, exist_ok=True)
    for name, numbers in [('Afocal Riflescope.ZMX', list(range(1, 18))),
                          ('Example 1, A singlet eyepiece.ZMX', [2, 3, 4])]:
        source = root / 'tests/data/zemax' / name
        original_hash = hashlib.sha256(source.read_bytes()).hexdigest()
        original = prescription(source)
        p = Project.import_zemax(source, executable=binary)
        s = p.system
        metadata = p.data['workspace']['zemaxImport']
        assert metadata['sourceSurfaceNumbers'] == numbers
        assert len(s['surfaces']) == len(numbers)
        for index, number in enumerate(numbers):
            row, actual = original[number], s['surfaces'][index]
            curvature = float(row['CURV'])
            close(actual['radius'], 1 / curvature if curvature else 0)
            close(actual['semiDiameter'], float(row['DIAM']))
            assert actual['material'] == row['material']
            distance = float(row['DISZ'])
            close(actual['thickness'] + (s['defocus'] if index == len(numbers)-1 else 0), distance)
            if row.get('stop'):
                assert s['stop'] == index
        assert len(s['fields']) == len(s['wavelengths']) == 3
        assert s['primary'] == 1
        if name.startswith('Afocal'):
            assert metadata['afocalImageSpace'] and not metadata['virtualImage']
            close(s['pupilDiameter'], 16.002)
            close(s['objectDistance'], 0)
            close(sum(x['thickness'] for x in s['surfaces']), 380.51135755203)
        else:
            assert metadata['virtualImage'] and metadata['floatByStop']
            close(s['objectDistance'], 24)
            close(s['defocus'], -1000)
            # Independent air/BK7/air matrix before the physical stop.
            catalog = {m['name']: m for m in p.data['materials']}
            glass = catalog['BK7']
            w = .5875618
            n = math.sqrt(1 + sum(b*w*w/(w*w-c) for b,c in zip(glass['b'],glass['c'])))
            A, B, C, D = 1., 0., 0., 1.
            before = 1.
            for index in range(2):
                row = original[index+2]
                after = n if index == 0 else 1.
                power = (after-before)*float(row['CURV'])
                C -= power*A
                D -= power*B
                A += float(row['DISZ'])/after*C
                B += float(row['DISZ'])/after*D
                before = after
            close(s['pupilDiameter'], 5/abs(A+B/24))
        native = output / (source.stem + '.optcad')
        p.save(native)
        reopened = Project.load(native, executable=binary)
        assert reopened.data == p.data
        analysis = reopened.analyze(pupil_grid=5)
        assert analysis['analysis_warning'] and analysis['experimental'] is True
        assert len(analysis['fields']) == len(s['fields'])
        # The warning must not change calculations. Compare the real imported
        # geometry with exactly the same geometry without Zemax metadata.
        plain = Project(p.data, executable=binary)
        plain.data['workspace'].pop('zemaxImport')
        plain.data['workspace'].pop('importNotes')
        expected = plain.analyze(pupil_grid=5)
        assert analysis['fields'] == expected['fields']
        calculation_outcomes = {}
        for operation in ('wavefront', 'diffraction', 'autofocus', 'optimize'):
            options = {'field': 0, 'size': 32, 'pupil_grid': 17 if operation == 'diffraction' else 9}
            outcomes = []
            for source_project in (p, plain):
                trial = Project(source_project.data, executable=binary)
                if operation == 'optimize':
                    # A small, valid plan keeps this regression deterministic.
                    trial.data['optimization'] = {
                        'variables': [{'parameter': 0, 'surface': 0, 'lower': -1000, 'upper': 1000, 'step': .01}],
                        'operands': [{'kind': 0, 'field': 0, 'target': 0, 'scale': 1, 'weight': 1}],
                        'iterations': 1, 'pupilGrid': 3, 'minimumThroughput': .01,
                        'refocus': False}
                try:
                    value = trial._request(operation, **options)
                    if source_project is p:
                        assert value['analysis_warning'] and value['experimental']
                    for key in ('analysis_warning', 'experimental', 'import_warnings'):
                        value.pop(key, None)
                    # Updated projects retain their own source metadata.
                    value.pop('project', None)
                    outcomes.append(('result', value))
                except OpticalCADError as error:
                    assert 'ТЕСТОВЫЙ РАСЧЁТ' not in str(error)
                    assert bool(error.analysis_warning) == (source_project is p)
                    outcomes.append(('calculation_error', str(error)))
            assert outcomes[0] == outcomes[1], (operation, outcomes)
            calculation_outcomes[operation] = {'status': outcomes[0][0]}
            if outcomes[0][0] == 'calculation_error':
                calculation_outcomes[operation]['error'] = outcomes[0][1]
        # Real rays are traced through the physical elements independently of
        # an afocal or virtual image interpretation. Check each chief ray.
        traced = p._request('trace_rays', field=0, pupils=[[0, 0], [0, 1]])['rays']
        for ray in traced:
            assert len(ray['points_world_mm']) >= len(numbers)+1
            assert all(math.isfinite(value) for value in ray['exit_direction'])
        if metadata['floatByStop']:
            close(traced[1]['exit_point_mm'][1], 2.5)
            assert not traced[1]['complete']  # physical trace succeeded, virtual projection intentionally unavailable
        assert hashlib.sha256(source.read_bytes()).hexdigest() == original_hash
        report.append({'source': name, 'sha256': original_hash,
            'surfaces_checked': len(numbers), 'stop_zemax': numbers[s['stop']],
            'pupil_diameter_native_mm': s['pupilDiameter'], 'object_distance_mm': s['objectDistance'],
            'image_z_native_mm': analysis['image_z_mm'], 'efl_mm': analysis.get('efl_mm'),
            'prescription_check': 'passed', 'native_roundtrip': 'passed', 'physical_ray_trace': 'passed',
            'warnings': metadata['warnings'], 'analysis_warning': analysis['analysis_warning'],
            'experimental': analysis['experimental'], 'calculation_outcomes': calculation_outcomes,
            'calculated_fields': analysis['fields']})
    (output / 'verification.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
print('Two real Zemax 13 files: prescription, native roundtrip and unblocked calculations with warnings passed')
