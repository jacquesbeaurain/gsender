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

import { Duplex } from "stream";
import GrblSimulator from "./GrblSimulator";

// The names the simulated boards are listed and connected under.
export const SIMULATOR_PORT = "Simulator";
export const SIMULATOR_HAL_PORT = "Simulator grblHAL";

export const isSimulatorPath = (path) =>
	path === SIMULATOR_PORT || path === SIMULATOR_HAL_PORT;

// The part of node-serialport's SerialPort that SerialConnection and the
// YMODEM sender use - a Duplex stream with open(), close() and isOpen - in
// front of a simulated board, so everything above it runs unchanged.
class SimulatedPort extends Duplex {
	isOpen = false;

	constructor(path) {
		super();
		this.path = path;
		this.simulator = new GrblSimulator({
			grblHal: path === SIMULATOR_HAL_PORT,
		});
		this.simulator.onData = (text) => {
			if (this.isOpen) {
				this.push(Buffer.from(text, "latin1"));
			}
		};
	}

	_read() {}

	_write(chunk, _encoding, callback) {
		this.simulator.send(chunk);
		callback();
	}

	// SerialPort.write() also takes arrays of bytes.
	write(data, encoding, callback) {
		if (Array.isArray(data)) {
			data = Buffer.from(data);
		}
		return super.write(data, encoding, callback);
	}

	// @param {function} callback The error-first callback.
	open(callback) {
		if (this.isOpen) {
			callback?.(new Error(`Port is already open "${this.path}"`));
			return;
		}
		this.isOpen = true;
		this.simulator.open();
		setImmediate(() => {
			this.emit("open");
			callback?.(null);
		});
	}

	// @param {function} callback The error-first callback.
	close(callback) {
		if (!this.isOpen) {
			callback?.(new Error(`Port is not open "${this.path}"`));
			return;
		}
		this.isOpen = false;
		this.simulator.close();
		setImmediate(() => {
			this.emit("close");
			callback?.(null);
		});
	}

	_destroy(error, callback) {
		this.isOpen = false;
		this.simulator.close();
		callback(error);
	}
}

export default SimulatedPort;
