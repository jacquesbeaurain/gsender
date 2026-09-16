import { toCSV, MeshPoint } from '../useMeshCapture';
import { METRIC_UNITS, IMPERIAL_UNITS } from 'app/constants';

// Probed in a serpentine, so the second row arrives right-to-left.
const points: MeshPoint[] = [
    { ix: 0, iy: 0, x: 0, y: 0, z: -1.5 },
    { ix: 1, iy: 0, x: 10, y: 0, z: -1.25 },
    { ix: 1, iy: 1, x: 10, y: 10, z: -2 },
    { ix: 0, iy: 1, x: 0, y: 10, z: -1.75 },
];

describe('toCSV', () => {
    it('writes a header and one row per point', () => {
        const lines = toCSV(points, METRIC_UNITS).split('\n');
        expect(lines[0]).toBe('X,Y,Z');
        expect(lines).toHaveLength(points.length + 1);
    });

    it('emits grid order regardless of the order probed', () => {
        expect(toCSV(points, METRIC_UNITS)).toBe(
            [
                'X,Y,Z',
                '0.000,0.000,-1.500',
                '10.000,0.000,-1.250',
                '0.000,10.000,-1.750',
                '10.000,10.000,-2.000',
            ].join('\n'),
        );
    });

    it('does not reorder the caller’s array', () => {
        const original = [...points];
        toCSV(points, METRIC_UNITS);
        expect(points).toEqual(original);
    });

    it('carries an extra decimal place in imperial', () => {
        const lines = toCSV([points[0]], IMPERIAL_UNITS).split('\n');
        expect(lines[1]).toBe('0.0000,0.0000,-1.5000');
    });

    it('handles an empty capture', () => {
        expect(toCSV([], METRIC_UNITS)).toBe('X,Y,Z');
    });
});
