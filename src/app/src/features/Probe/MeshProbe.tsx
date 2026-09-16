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

import { useCallback, useEffect, useState } from 'react';
import cx from 'classnames';

import {
    Dialog,
    DialogContent,
    DialogHeader,
    DialogTitle,
} from 'app/components/shadcn/Dialog';
import { Input } from 'app/components/shadcn/Input';
import { Button } from 'app/components/Button';
import { useTypedSelector } from 'app/hooks/useTypedSelector';
import { toast } from 'app/lib/toaster';
import { METRIC_UNITS } from 'app/constants';

import ProbeCircuitStatus from './ProbeCircuitStatus';
import MeshGridIcon from './MeshGridIcon';
import { Actions, State } from './definitions';
import { getWidgetConfigContext } from '../WidgetConfig/WidgetContextProvider';
import { downloadCSV, toCSV, useMeshCapture } from './useMeshCapture';

interface MeshProbeProps {
    state: State;
    actions: Actions;
}

const clampInt = (value: string, min: number, max: number): number => {
    const parsed = Number.parseInt(value, 10);
    if (Number.isNaN(parsed)) {
        return min;
    }
    return Math.min(Math.max(parsed, min), max);
};

const MeshProbe = ({ state, actions }: MeshProbeProps) => {
    const { canClick, showMesh, connectionMade, connectivityTest, units } =
        state;
    const { actions: config } = getWidgetConfigContext();

    const { probePinStatus, isConnected } = useTypedSelector((reduxState) => ({
        probePinStatus: reduxState.controller.state.status?.pinState.P ?? false,
        isConnected: reduxState.connection.isConnected,
    }));

    const [dx, setDx] = useState('10');
    const [nx, setNx] = useState('4');
    const [dy, setDy] = useState('10');
    const [ny, setNy] = useState('4');
    const [filename, setFilename] = useState('mesh');

    const { status, points, total, error, start, stop, reset } =
        useMeshCapture();

    // Same connectivity gate as the single-axis probe dialog: the machine has
    // to have reported the probe circuit closing at least once.
    useEffect(() => {
        if (!showMesh) {
            return;
        }
        if (!connectivityTest || probePinStatus) {
            actions.setProbeConnectivity(true);
        }
    }, [showMesh, connectivityTest, probePinStatus, actions]);

    const running = status === 'running';

    const handleOpenChange = useCallback(
        (isOpen: boolean) => {
            if (!isOpen && running) {
                // Never let the dialog close out from under a live capture -
                // the Stop button is the only way out.
                return;
            }
            if (!isOpen) {
                reset();
            }
            actions.onMeshOpenChange(isOpen);
        },
        [actions, running, reset],
    );

    const handleStart = useCallback(() => {
        const columns = clampInt(nx, 1, 200);
        const rows = clampInt(ny, 1, 200);
        const spacingX = Number(dx);
        const spacingY = Number(dy);

        if (!Number.isFinite(spacingX) || !Number.isFinite(spacingY)) {
            toast.error('Spacing must be a number', {
                position: 'bottom-right',
            });
            return;
        }

        start({
            dx: spacingX,
            nx: columns,
            dy: spacingY,
            ny: rows,
            units,
            probeFeedrate: Number(config.get('probeFastFeedrate', 150)),
            probeDistance: Number(config.get('zProbeDistance', 30)),
        });
        toast.info(`Capturing a ${columns} x ${rows} mesh`, {
            position: 'bottom-right',
        });
    }, [dx, nx, dy, ny, units, config, start]);

    const handleSave = useCallback(() => {
        if (!points.length) {
            return;
        }
        downloadCSV(filename, toCSV(points, units));
        toast.info(`Saved ${points.length} points`, {
            position: 'bottom-right',
        });
    }, [points, filename, units]);

    // A finished capture is the thing the user came for, so write it out
    // without making them ask.
    useEffect(() => {
        if (status === 'done' && points.length) {
            downloadCSV(filename, toCSV(points, units));
            toast.info(`Captured ${points.length} points`, {
                position: 'bottom-right',
            });
        }
        // Only when the run ends, not on every point.
        // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [status]);

    const unitLabel = units === METRIC_UNITS ? 'mm' : 'in';
    const gridSize = clampInt(nx, 1, 200) * clampInt(ny, 1, 200);

    const statusLine = (): string => {
        if (running) {
            return `Probing point ${points.length + 1} of ${total}`;
        }
        if (status === 'done') {
            return `Captured ${points.length} points`;
        }
        if (status === 'stopped') {
            return `Stopped after ${points.length} of ${total} points`;
        }
        if (status === 'failed') {
            return error || 'Capture failed';
        }
        return `${gridSize} points`;
    };

    return (
        <Dialog open={showMesh} onOpenChange={handleOpenChange}>
            <DialogContent
                className={cx(
                    'flex flex-col justify-center items-center bg-gray-100 w-[650px] min-h-[450px] p-4',
                    { hidden: !showMesh },
                )}
            >
                <DialogHeader className="text-robin-700 flex items-start justify-center">
                    <DialogTitle>Capture Mesh with 3D Probe</DialogTitle>
                </DialogHeader>

                <div className="grid grid-cols-[1.5fr_1fr] gap-4 w-[600px] min-h-[240px]">
                    <div className="flex flex-col justify-between pb-2">
                        <div className="text-black leading-snug dark:text-white">
                            <p className="mb-3">
                                1. Jog the probe over the first point of the
                                mesh, at a height it can safely travel across
                                the object at.
                            </p>
                            <p className="mb-3">
                                2. Gently push the probe needle to check the
                                circuit is triggered properly (indicated by a
                                green light).
                            </p>
                            <p className="mb-3">
                                3. The mesh runs from there in +X and +Y, and
                                the tool returns to this height between points.
                            </p>
                        </div>

                        <div className="grid grid-cols-2 gap-3">
                            <Input
                                label="X spacing"
                                sizing="sm"
                                suffix={unitLabel}
                                type="number"
                                value={dx}
                                disabled={running}
                                onChange={(e) => setDx(e.target.value)}
                            />
                            <Input
                                label="X points"
                                sizing="sm"
                                type="number"
                                value={nx}
                                disabled={running}
                                onChange={(e) => setNx(e.target.value)}
                            />
                            <Input
                                label="Y spacing"
                                sizing="sm"
                                suffix={unitLabel}
                                type="number"
                                value={dy}
                                disabled={running}
                                onChange={(e) => setDy(e.target.value)}
                            />
                            <Input
                                label="Y points"
                                sizing="sm"
                                type="number"
                                value={ny}
                                disabled={running}
                                onChange={(e) => setNy(e.target.value)}
                            />
                        </div>

                        <div className="mt-3">
                            <Input
                                label="Output file"
                                sizing="sm"
                                suffix=".csv"
                                value={filename}
                                disabled={running}
                                onChange={(e) => setFilename(e.target.value)}
                            />
                        </div>
                    </div>

                    <div className="flex flex-col items-center justify-between">
                        <MeshGridIcon className="w-16 h-16 text-gray-500 dark:text-content-secondary mt-2" />
                        <ProbeCircuitStatus
                            connected={isConnected}
                            probeActive={probePinStatus}
                        />
                    </div>
                </div>

                <div className="w-[600px] flex flex-col gap-2">
                    <span
                        className={cx('text-sm', {
                            'text-red-600 dark:text-red-400':
                                status === 'failed',
                            'text-gray-700 dark:text-content-secondary':
                                status !== 'failed',
                        })}
                    >
                        {statusLine()}
                    </span>

                    <div className="flex gap-2">
                        {running ? (
                            <Button
                                variant="error"
                                onClick={stop}
                                className="flex-1"
                            >
                                Stop
                            </Button>
                        ) : (
                            <Button
                                variant="primary"
                                disabled={!canClick || !connectionMade}
                                onClick={handleStart}
                                className="flex-1"
                            >
                                {connectionMade
                                    ? 'Start Capture'
                                    : 'Waiting for probe circuit check...'}
                            </Button>
                        )}
                        <Button
                            disabled={running || !points.length}
                            onClick={handleSave}
                        >
                            Save CSV
                        </Button>
                    </div>
                </div>
            </DialogContent>
        </Dialog>
    );
};

export default MeshProbe;
