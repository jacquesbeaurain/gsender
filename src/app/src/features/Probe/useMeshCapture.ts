/*
 * Copyright (C) 2021 Sienci Labs Inc.
 *
 * This file is part of gSender.
 *
 * gSender is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, under version 3 of the License.
 *
 * gSender is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with gSender.  If not, see <https://www.gnu.org/licenses/>.
 *
 * Contact for information regarding this program and its license
 * can be sent through gSender@sienci.com or mailed to the main office
 * of Sienci Labs Inc. in Waterloo, Ontario, Canada.
 *
 */

import { useCallback, useEffect, useRef, useState } from 'react';

import controller from 'app/lib/controller';
import reduxStore from 'app/store/redux';
import { METRIC_UNITS } from 'app/constants';
import { UNITS_EN } from 'app/definitions/general';

export interface MeshOptions {
    dx: number;
    nx: number;
    dy: number;
    ny: number;
    units: UNITS_EN;
    // Feedrate for the probing move itself.
    probeFeedrate: number;
    // How far below the starting Z a probe is allowed to travel before it
    // gives up and the machine alarms.
    probeDistance: number;
}

export interface MeshPoint {
    // Grid indices, so a caller can lay the points back out as a mesh.
    ix: number;
    iy: number;
    // Work coordinates.
    x: number;
    y: number;
    z: number;
}

export type MeshStatus = 'idle' | 'running' | 'done' | 'stopped' | 'failed';

// A probe move is short, but the rapid that precedes it can cross the whole
// table, so allow generously before calling it a lost machine.
const PROBE_TIMEOUT_MS = 120000;

// [PRB:0.000,0.000,-10.000:1] - trailing flag is 1 on contact, 0 on a miss.
const PRB_RE = /^\[PRB:(-?[\d.]+),(-?[\d.]+),(-?[\d.]+):([01])\]$/;

/**
 * Walks a rectangular grid, probing down at each point and collecting the
 * contact positions.
 *
 * The grid starts at wherever the tool is parked when the capture begins -
 * that position is both the first point and the safe height the tool returns
 * to between points - and extends +X and +Y from there.
 *
 * Points are visited in a serpentine so the tool is not dragged back across
 * the work at the end of every row. Results are stored with their grid indices
 * so the caller can still write them out in reading order.
 */
export function useMeshCapture() {
    const [status, setStatus] = useState<MeshStatus>('idle');
    const [points, setPoints] = useState<MeshPoint[]>([]);
    const [total, setTotal] = useState(0);
    const [error, setError] = useState<string>(null);

    // The loop is driven by promises rather than renders, so it reads its stop
    // flag from a ref.
    const stopRequestedRef = useRef(false);
    const runningRef = useRef(false);

    useEffect(() => {
        return () => {
            stopRequestedRef.current = true;
        };
    }, []);

    /**
     * Resolves with the next [PRB:...] line the controller reports, or rejects
     * if the machine alarms, errors or goes quiet.
     */
    const waitForProbe = useCallback((): Promise<{
        x: number;
        y: number;
        z: number;
        contact: boolean;
    }> => {
        return new Promise((resolve, reject) => {
            let settled = false;

            const finish = (fn: () => void) => {
                if (settled) {
                    return;
                }
                settled = true;
                clearTimeout(timer);
                controller.removeListener('serialport:read', onRead);
                fn();
            };

            const onRead = (line: string) => {
                const data = String(line).trim();
                const match = PRB_RE.exec(data);
                if (match) {
                    finish(() =>
                        resolve({
                            x: Number(match[1]),
                            y: Number(match[2]),
                            z: Number(match[3]),
                            contact: match[4] === '1',
                        }),
                    );
                    return;
                }
                if (/^ALARM:/.test(data) || /^error:/.test(data)) {
                    finish(() => reject(new Error(data)));
                }
            };

            const timer = setTimeout(() => {
                finish(() =>
                    reject(new Error('Timed out waiting for a probe result')),
                );
            }, PROBE_TIMEOUT_MS);

            controller.addListener('serialport:read', onRead);
        });
    }, []);

    const start = useCallback(
        async (options: MeshOptions) => {
            if (runningRef.current) {
                return;
            }
            const { dx, nx, dy, ny, units, probeFeedrate, probeDistance } =
                options;

            runningRef.current = true;
            stopRequestedRef.current = false;
            setPoints([]);
            setError(null);
            setTotal(nx * ny);
            setStatus('running');

            const modal = units === METRIC_UNITS ? 'G21' : 'G20';
            const fixed = units === METRIC_UNITS ? 3 : 4;
            const num = (value: number) => value.toFixed(fixed);

            // Where the tool is parked now is the mesh origin, the safe height
            // between points, and the top of the probe travel.
            const { mpos, wpos } = reduxStore.getState().controller;
            const origin = {
                x: Number(wpos.x),
                y: Number(wpos.y),
                z: Number(wpos.z),
            };
            // PRB comes back in machine coordinates. Offsets do not move
            // during a capture, so one conversion taken up front holds for
            // every point.
            const wco = {
                x: Number(mpos.x) - origin.x,
                y: Number(mpos.y) - origin.y,
                z: Number(mpos.z) - origin.z,
            };
            const probeFloor = origin.z - Math.abs(probeDistance);

            const collected: MeshPoint[] = [];
            let failed = false;

            try {
                for (let iy = 0; iy < ny; iy += 1) {
                    // Serpentine: every other row runs right to left.
                    const forward = iy % 2 === 0;
                    for (let step = 0; step < nx; step += 1) {
                        if (stopRequestedRef.current) {
                            setStatus('stopped');
                            return;
                        }

                        const ix = forward ? step : nx - 1 - step;
                        const x = origin.x + ix * dx;
                        const y = origin.y + iy * dy;

                        // The probe is the last line queued: anything behind
                        // it would be sent to an alarm-locked machine if the
                        // probe missed, and come back as error:9. The retract
                        // is the first line of the next point instead, and the
                        // last point is covered on the way out below.
                        const probeResult = waitForProbe();
                        controller.command('gcode', [
                            `${modal} G90 G0 Z${num(origin.z)}`,
                            `${modal} G90 G0 X${num(x)} Y${num(y)}`,
                            `${modal} G38.2 Z${num(probeFloor)} F${num(probeFeedrate)}`,
                        ]);

                        const result = await probeResult;
                        if (!result.contact) {
                            throw new Error(
                                `No contact at X${num(x)} Y${num(y)} - nothing within ${num(Math.abs(probeDistance))} below the start height`,
                            );
                        }

                        collected.push({
                            ix,
                            iy,
                            x: result.x - wco.x,
                            y: result.y - wco.y,
                            z: result.z - wco.z,
                        });
                        setPoints([...collected]);
                    }
                }

                if (stopRequestedRef.current) {
                    setStatus('stopped');
                    return;
                }
                setStatus('done');
            } catch (err) {
                failed = true;
                setError(err instanceof Error ? err.message : String(err));
                setStatus('failed');
            } finally {
                // Retract clear of the work on the way out - but not after a
                // failure, which usually means the machine is alarm-locked and
                // would only answer a move with "error:9".
                if (!failed) {
                    controller.command('gcode', [
                        `${modal} G90 G0 Z${num(origin.z)}`,
                    ]);
                }
                runningRef.current = false;
            }
        },
        [waitForProbe],
    );

    /**
     * Graceful stop: the probe already in flight is allowed to finish and be
     * recorded, then the tool retracts and the loop ends. Points captured so
     * far are kept.
     */
    const stop = useCallback(() => {
        stopRequestedRef.current = true;
    }, []);

    const reset = useCallback(() => {
        stopRequestedRef.current = false;
        setPoints([]);
        setTotal(0);
        setError(null);
        setStatus('idle');
    }, []);

    return { status, points, total, error, start, stop, reset };
}

/** Grid order (Y then X), regardless of the order the points were probed in. */
export function toCSV(points: MeshPoint[], units: UNITS_EN): string {
    const fixed = units === METRIC_UNITS ? 3 : 4;
    const rows = [...points].sort((a, b) => a.iy - b.iy || a.ix - b.ix);
    return [
        'X,Y,Z',
        ...rows.map((p) =>
            [p.x, p.y, p.z].map((v) => v.toFixed(fixed)).join(','),
        ),
    ].join('\n');
}

export function downloadCSV(filename: string, contents: string): void {
    const name = filename.trim() || 'mesh';
    const blob = new Blob([contents], { type: 'text/csv' });
    const link = document.createElement('a');
    link.href = URL.createObjectURL(blob);
    link.setAttribute('download', name.endsWith('.csv') ? name : `${name}.csv`);
    document.body.appendChild(link);
    link.click();
    document.body.removeChild(link);
    URL.revokeObjectURL(link.href);
}
