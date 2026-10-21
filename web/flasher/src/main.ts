import { ESPLoader, Transport, type IEspLoaderTerminal } from 'esptool-js';
import { FIRMWARE_URL, manifest, validateFirmware } from './firmware.ts';
import { assertCompatibleDevice, installFactoryImage } from './installer.ts';

const must = <T extends HTMLElement>(id: string): T => {
  const element = document.getElementById(id);
  if (!element) throw new Error(`Falta el elemento #${id}`);
  return element as T;
};

const connectButton = must<HTMLButtonElement>('connectButton');
const disconnectButton = must<HTMLButtonElement>('disconnectButton');
const flashButton = must<HTMLButtonElement>('flashButton');
const safetyCheck = must<HTMLInputElement>('safetyCheck');
const compatibilityWarning = must<HTMLDivElement>('compatibilityWarning');
const connectStep = must<HTMLLIElement>('connectStep');
const reviewStep = must<HTMLLIElement>('reviewStep');
const flashStep = must<HTMLLIElement>('flashStep');
const statusText = must<HTMLSpanElement>('statusText');
const progressValue = must<HTMLElement>('progressValue');
const progressTrack = must<HTMLDivElement>('progressTrack');
const progressBar = must<HTMLSpanElement>('progressBar');
const resultPanel = must<HTMLDivElement>('resultPanel');
const terminalElement = must<HTMLPreElement>('terminal');
const logButton = must<HTMLButtonElement>('logButton');
const confirmDialog = must<HTMLDialogElement>('confirmDialog');

let transport: Transport | null = null;
let loader: ESPLoader | null = null;
let connected = false;
let busy = false;
let firmwareCache: Uint8Array | null = null;
const serialAvailable = 'serial' in navigator && window.isSecureContext && !/Android|iPhone|iPad|Mobile/i.test(navigator.userAgent);

document.querySelectorAll<HTMLAnchorElement>('[data-download]').forEach((link) => {
  link.href = FIRMWARE_URL;
  link.download = manifest.filename;
});

const appendLog = (message: string): void => {
  const normalized = message.replace(/\u001b\[[0-9;]*m/g, '').trimEnd();
  if (!normalized) return;
  terminalElement.textContent += `${normalized}\n`;
  terminalElement.scrollTop = terminalElement.scrollHeight;
};

const terminal: IEspLoaderTerminal = {
  clean: () => { terminalElement.textContent = ''; },
  writeLine: appendLog,
  write: appendLog,
};

const setProgress = (percent: number, label: string): void => {
  const safePercent = Math.max(0, Math.min(100, Math.round(percent)));
  statusText.textContent = label;
  progressValue.textContent = `${safePercent}%`;
  progressBar.style.width = `${safePercent}%`;
  progressTrack.setAttribute('aria-valuenow', String(safePercent));
  progressTrack.classList.remove('is-indeterminate');
};

const setIndeterminate = (label: string): void => {
  statusText.textContent = label;
  progressValue.textContent = '···';
  progressTrack.classList.add('is-indeterminate');
  progressTrack.removeAttribute('aria-valuenow');
};

const setError = (error: unknown): void => {
  const raw = error instanceof Error ? error.message : String(error);
  let message = raw;
  if (/No port selected|NotFoundError|chooser/i.test(raw)) {
    message = 'No se seleccionó ningún puerto USB.';
  } else if (/timeout|Timed out|Read timeout/i.test(raw)) {
    message = 'No se pudo entrar en modo de flasheo. Desconecta, vuelve a conectar y reintenta.';
  } else if (/NetworkError|Failed to open serial port/i.test(raw)) {
    message = 'El puerto está ocupado. Cierra monitores serie u otras herramientas y reintenta.';
  }
  statusText.textContent = message;
  progressValue.textContent = '!';
  progressTrack.classList.remove('is-indeterminate');
  progressBar.style.width = '0%';
  appendLog(`ERROR: ${raw}`);
};

const updateControls = (): void => {
  const canFlash = connected && loader !== null && safetyCheck.checked && !busy;
  flashButton.disabled = !canFlash;
  connectButton.disabled = busy || connected || !serialAvailable;
  safetyCheck.disabled = busy;
  disconnectButton.hidden = !connected || busy;
};

const setBusy = (value: boolean): void => {
  busy = value;
  document.body.dataset.busy = String(value);
  updateControls();
};

const loadAndValidateFirmware = async (): Promise<Uint8Array> => {
  if (firmwareCache) return firmwareCache;
  setIndeterminate('Descargando y validando el firmware…');
  const response = await fetch(FIRMWARE_URL, { cache: 'no-store' });
  if (!response.ok) throw new Error(`No se pudo descargar el firmware (${response.status}).`);
  const firmware = new Uint8Array(await response.arrayBuffer());
  await validateFirmware(firmware);
  appendLog(`Imagen completa validada: ${firmware.length} bytes`);
  appendLog(`SHA-256: ${manifest.sha256}`);
  firmwareCache = firmware;
  return firmware;
};

const resetConnectionUi = (): void => {
  connected = false;
  loader = null;
  transport = null;
  connectStep.classList.remove('is-complete');
  connectStep.classList.add('is-current');
  reviewStep.classList.remove('is-current', 'is-complete');
  flashStep.classList.remove('is-current', 'is-complete');
  connectButton.textContent = 'Conectar por USB';
  safetyCheck.checked = false;
  updateControls();
};

const disconnect = async (): Promise<void> => {
  if (transport) {
    try { await transport.disconnect(); } catch { /* El puerto puede haberse cerrado al reiniciar. */ }
  }
  resetConnectionUi();
  setProgress(0, 'Dispositivo desconectado');
  appendLog('Puerto USB desconectado.');
};

const connectDevice = async (): Promise<void> => {
  if (!serialAvailable || busy || connected) return;
  setBusy(true);
  resultPanel.hidden = true;
  terminal.clean();
  setIndeterminate('Selecciona el puerto del Cardputer…');
  try {
    const port = await navigator.serial.requestPort();
    transport = new Transport(port, false);
    transport.setDeviceLostCallback(() => {
      if (!busy) {
        resetConnectionUi();
        setError(new Error('Se perdió la conexión USB.'));
      }
    });
    loader = new ESPLoader({
      transport,
      baudrate: 460800,
      terminal,
      debugLogging: false,
      enableTracing: false,
    });
    setIndeterminate('Detectando el chip…');
    const chipName = await loader.main();
    if (!/ESP32-S3/i.test(chipName)) {
      throw new Error(`Dispositivo incompatible: ${chipName || 'chip desconocido'}. Se requiere ESP32-S3.`);
    }
    setIndeterminate('Comprobando memoria y seguridad del dispositivo…');
    await assertCompatibleDevice(loader);
    await loadAndValidateFirmware();
    connected = true;
    connectStep.classList.remove('is-current');
    connectStep.classList.add('is-complete');
    reviewStep.classList.add('is-current');
    connectButton.textContent = 'ESP32-S3 conectado';
    setProgress(0, 'ESP32-S3 · 8 MB. Revisa el aviso de borrado para continuar.');
    appendLog('Instalación completa en 0x0. No se requiere ningún firmware previo.');
    appendLog('Se borrará toda la flash, incluidas Wi-Fi, sesión y preferencias.');
  } catch (error) {
    if (transport) {
      try { await transport.disconnect(); } catch { /* No hay nada más que cerrar. */ }
    }
    resetConnectionUi();
    setError(error);
  } finally {
    setBusy(false);
  }
};

const askForConfirmation = (): Promise<boolean> => new Promise((resolve) => {
  const onClose = (): void => {
    confirmDialog.removeEventListener('close', onClose);
    resolve(confirmDialog.returnValue === 'confirm');
  };
  confirmDialog.addEventListener('close', onClose);
  confirmDialog.returnValue = 'cancel';
  confirmDialog.showModal();
});

const flashFirmware = async (): Promise<void> => {
  if (!loader || !transport || !connected || !safetyCheck.checked || busy) return;
  setBusy(true);
  if (!(await askForConfirmation())) { setBusy(false); return; }
  resultPanel.hidden = true;
  reviewStep.classList.remove('is-current');
  reviewStep.classList.add('is-complete');
  flashStep.classList.add('is-current');
  window.addEventListener('beforeunload', preventNavigation);
  try {
    const firmware = await loadAndValidateFirmware();
    await installFactoryImage(loader, firmware, phase => {
      const labels = {
        validating: 'Validando la imagen y el dispositivo antes del borrado…',
        erasing: 'Borrando toda la flash. No desconectes el cable…',
        writing: 'Instalando CardGPT completo…',
        verifying: 'Verificando los 8 MB escritos…',
      };
      setIndeterminate(labels[phase]);
      appendLog(labels[phase]);
    }, percent => setProgress(percent, 'Instalando CardGPT completo…'));
    setIndeterminate('Reiniciando el Cardputer…');
    let restarted = true;
    try { await loader.after('hard_reset'); }
    catch { restarted = false; appendLog('Imagen verificada. Apaga y enciende el dispositivo manualmente.'); }
    setProgress(100, restarted ? 'Instalación verificada. Configura la Wi-Fi y vincula tu cuenta.' : 'Instalación verificada. Apaga y enciende el Cardputer.');
    flashStep.classList.remove('is-current');
    flashStep.classList.add('is-complete');
    resultPanel.hidden = false;
    appendLog('Instalación finalizada y verificada.');
    connected = false;
    try { await transport.disconnect(); } catch { /* Reiniciar puede cerrar el puerto primero. */ }
    loader = null;
    transport = null;
    connectButton.textContent = 'Conectar por USB';
    safetyCheck.checked = false;
  } catch (error) {
    if (transport) {
      try { await transport.disconnect(); } catch { /* Puede haberse perdido el USB. */ }
    }
    resetConnectionUi();
    setError(error);
    appendLog('Si el borrado llegó a iniciarse, reconecta en modo descarga y repite la instalación completa.');
  } finally {
    window.removeEventListener('beforeunload', preventNavigation);
    setBusy(false);
  }
};

const preventNavigation = (event: BeforeUnloadEvent): void => {
  event.preventDefault();
  event.returnValue = '';
};

compatibilityWarning.hidden = serialAvailable;
if (!serialAvailable) {
  connectButton.disabled = true;
  statusText.textContent = 'Necesitas Chrome o Edge de escritorio por HTTPS';
}

safetyCheck.addEventListener('change', () => {
  reviewStep.classList.toggle('is-complete', safetyCheck.checked && connected);
  flashStep.classList.toggle('is-current', safetyCheck.checked && connected);
  updateControls();
});
connectButton.addEventListener('click', connectDevice);
disconnectButton.addEventListener('click', disconnect);
flashButton.addEventListener('click', flashFirmware);
logButton.addEventListener('click', () => {
  const open = terminalElement.hidden;
  terminalElement.hidden = !open;
  logButton.setAttribute('aria-expanded', String(open));
  logButton.textContent = open ? 'Ocultar registro técnico' : 'Ver registro técnico';
});

updateControls();
