import { ESPLoader, Transport, type IEspLoaderTerminal } from 'esptool-js';
import {
  APPLICATION_URL,
  FACTORY_URL,
  manifest,
  validateApplication,
  validateFactoryImage,
} from './firmware.ts';
import {
  discoverInstallPlan,
  installFactoryImage,
  installIntoLauncher,
  type InstallPhase,
  type InstallPlan,
} from './installer.ts';

const must = <T extends HTMLElement>(id: string): T => {
  const element = document.getElementById(id);
  if (!element) throw new Error(`Falta el elemento #${id}`);
  return element as T;
};

const connectButton = must<HTMLButtonElement>('connectButton');
const disconnectButton = must<HTMLButtonElement>('disconnectButton');
const flashButton = must<HTMLButtonElement>('flashButton');
const safetyCheck = must<HTMLInputElement>('safetyCheck');
const safetyLabel = must<HTMLLabelElement>('safetyLabel');
const safetyText = must<HTMLSpanElement>('safetyText');
const installNotice = must<HTMLParagraphElement>('installNotice');
const installerIntro = must<HTMLParagraphElement>('installerIntro');
const compatibilityWarning = must<HTMLDivElement>('compatibilityWarning');
const connectStep = must<HTMLLIElement>('connectStep');
const reviewStep = must<HTMLLIElement>('reviewStep');
const flashStep = must<HTMLLIElement>('flashStep');
const statusText = must<HTMLSpanElement>('statusText');
const statusRow = must<HTMLDivElement>('statusRow');
const progressValue = must<HTMLElement>('progressValue');
const progressTrack = must<HTMLDivElement>('progressTrack');
const progressBar = must<HTMLSpanElement>('progressBar');
const resultPanel = must<HTMLDivElement>('resultPanel');
const resultTitle = must<HTMLElement>('resultTitle');
const resultText = must<HTMLElement>('resultText');
const terminalElement = must<HTMLPreElement>('terminal');
const logButton = must<HTMLButtonElement>('logButton');
const confirmDialog = must<HTMLDialogElement>('confirmDialog');
const confirmTitle = must<HTMLElement>('confirmTitle');
const confirmText = must<HTMLElement>('confirmText');
const confirmButton = must<HTMLButtonElement>('confirmButton');
const firmwareInfoButton = must<HTMLButtonElement>('firmwareInfoButton');
const firmwareInfoDialog = must<HTMLDialogElement>('firmwareInfoDialog');

let transport: Transport | null = null;
let loader: ESPLoader | null = null;
let connected = false;
let busy = false;
let plan: InstallPlan | null = null;
let applicationCache: Uint8Array | null = null;
let factoryCache: Uint8Array | null = null;
const serialAvailable = 'serial' in navigator && window.isSecureContext &&
  !/Android|iPhone|iPad|Mobile/i.test(navigator.userAgent);

document.querySelectorAll<HTMLAnchorElement>('[data-download="application"]').forEach(link => {
  link.href = APPLICATION_URL;
  link.download = manifest.application.filename;
});
document.querySelectorAll<HTMLAnchorElement>('[data-download="factory"]').forEach(link => {
  link.href = FACTORY_URL;
  link.download = manifest.factory.filename;
});

const appendLog = (message: string): void => {
  const normalized = message.replace(/\u001b\[[0-9;]*m/g, '').trimEnd();
  if (!normalized) return;
  terminalElement.textContent += `${normalized}\n`;
  terminalElement.scrollTop = terminalElement.scrollHeight;
  logButton.hidden = false;
};

const terminal: IEspLoaderTerminal = {
  clean: () => {
    terminalElement.textContent = '';
    terminalElement.hidden = true;
    logButton.hidden = true;
    logButton.setAttribute('aria-expanded', 'false');
    logButton.textContent = 'Ver registro técnico';
  },
  writeLine: appendLog,
  write: appendLog,
};

const setProgress = (percent: number, label: string): void => {
  statusRow.hidden = false;
  progressTrack.hidden = false;
  const safePercent = Math.max(0, Math.min(100, Math.round(percent)));
  statusText.textContent = label;
  progressValue.textContent = `${safePercent}%`;
  progressBar.style.width = `${safePercent}%`;
  progressTrack.setAttribute('aria-valuenow', String(safePercent));
  progressTrack.classList.remove('is-indeterminate');
};

const setIndeterminate = (label: string): void => {
  statusRow.hidden = false;
  progressTrack.hidden = false;
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
  statusRow.hidden = false;
  progressTrack.hidden = false;
  statusText.textContent = message;
  progressValue.textContent = '!';
  progressTrack.classList.remove('is-indeterminate');
  progressBar.style.width = '0%';
  appendLog(`ERROR: ${raw}`);
};

const updateControls = (): void => {
  const canFlash = connected && loader !== null && plan !== null && safetyCheck.checked && !busy;
  flashButton.disabled = !canFlash;
  connectButton.disabled = busy || connected || !serialAvailable;
  safetyCheck.disabled = busy || !connected || plan === null;
  disconnectButton.hidden = !connected || busy;
};

const setBusy = (value: boolean): void => {
  busy = value;
  document.body.dataset.busy = String(value);
  updateControls();
};

const fetchBinary = async (
  url: string,
  label: string,
  validate: (data: Uint8Array) => Promise<void>,
): Promise<Uint8Array> => {
  setIndeterminate(`Descargando y validando ${label}…`);
  const response = await fetch(url, { cache: 'no-store' });
  if (!response.ok) throw new Error(`No se pudo descargar ${label} (${response.status}).`);
  const data = new Uint8Array(await response.arrayBuffer());
  await validate(data);
  appendLog(`${label} validado: ${data.length} bytes`);
  return data;
};

const loadApplication = async (): Promise<Uint8Array> => {
  applicationCache ??= await fetchBinary(APPLICATION_URL, 'CardGPT.bin', validateApplication);
  return applicationCache;
};

const loadFactory = async (): Promise<Uint8Array> => {
  factoryCache ??= await fetchBinary(FACTORY_URL, 'CardGPT-full.bin', validateFactoryImage);
  return factoryCache;
};

const showDefaultCopy = (): void => {
  installerIntro.textContent = 'Conecta el dispositivo. Antes de mostrar la confirmación, detectaremos si debe preservarse M5Launcher.';
  installNotice.hidden = true;
  safetyLabel.hidden = true;
  flashButton.hidden = true;
  statusRow.hidden = true;
  progressTrack.hidden = true;
  flashButton.innerHTML = 'Instalar CardGPT <span aria-hidden="true">↓</span>';
};

const showPlan = (selected: InstallPlan): void => {
  installNotice.hidden = false;
  safetyLabel.hidden = false;
  flashButton.hidden = false;
  if (selected.mode === 'launcher') {
    const verb = selected.action === 'update' ? 'actualizará' : 'creará una nueva ranura para';
    installerIntro.textContent = 'M5Launcher detectado. Se ha elegido automáticamente el modo de conservación.';
    installNotice.innerHTML = `<strong>M5Launcher y los demás firmwares se conservarán.</strong><br>La web ${verb} CardGPT en 0x${selected.target.offset.toString(16).toUpperCase()}. NVS, Wi-Fi, sesión y ajustes no se borran.`;
    safetyText.innerHTML = 'He conectado un Cardputer ADV y autorizo escribir <strong>solo CardGPT</strong> y, si es una instalación nueva, registrar su ranura. No se reemplazará otro firmware.';
    flashButton.innerHTML = `${selected.action === 'update' ? 'Actualizar' : 'Añadir'} CardGPT <span aria-hidden="true">↓</span>`;
    confirmTitle.textContent = selected.action === 'update' ? '¿Actualizar CardGPT?' : '¿Añadir CardGPT a M5Launcher?';
    confirmText.innerHTML = `<strong>Se conserva M5Launcher, NVS y el resto de firmwares.</strong> Solo se escribirá CardGPT en 0x${selected.target.offset.toString(16).toUpperCase()}${selected.tableChanged ? ' y la tabla se actualizará para registrar esa ranura' : ''}.`;
    confirmButton.textContent = selected.action === 'update' ? 'Sí, actualizar' : 'Sí, añadir';
    resultTitle.textContent = 'CardGPT instalado y verificado en M5Launcher.';
    resultText.textContent = ' Selecciónalo desde el launcher. Si no reinicia automáticamente, apaga y enciende el Cardputer.';
  } else {
    installerIntro.textContent = 'No se ha detectado M5Launcher. Está disponible la instalación independiente desde cero.';
    installNotice.innerHTML = '<strong>Instalación limpia: se borrará toda la flash.</strong><br>Se eliminarán el firmware actual, Wi-Fi, sesión y ajustes. Este modo no se ofrece cuando se detecta M5Launcher.';
    safetyText.innerHTML = 'He conectado un Cardputer ADV, tengo copia de lo importante y acepto borrar <strong>todos los firmwares y datos</strong> para instalar CardGPT desde cero.';
    flashButton.innerHTML = 'Borrar e instalar CardGPT <span aria-hidden="true">↓</span>';
    confirmTitle.textContent = '¿Borrar e instalar CardGPT?';
    confirmText.innerHTML = '<strong>Se borrará toda la flash interna:</strong> firmware actual, sesión, Wi-Fi y ajustes. Después se instalará la imagen completa de CardGPT.';
    confirmButton.textContent = 'Sí, borrar e instalar';
    resultTitle.textContent = 'CardGPT instalado y verificado.';
    resultText.textContent = ' Configura la Wi-Fi y vincula tu cuenta. Si no reinicia automáticamente, apaga y enciende el Cardputer.';
  }
};

const resetConnectionUi = (): void => {
  connected = false;
  plan = null;
  loader = null;
  transport = null;
  connectStep.classList.remove('is-complete');
  connectStep.classList.add('is-current');
  reviewStep.classList.remove('is-current', 'is-complete');
  flashStep.classList.remove('is-current', 'is-complete');
  connectButton.textContent = 'Conectar por USB';
  safetyCheck.checked = false;
  showDefaultCopy();
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
    setIndeterminate('Detectando M5Launcher y buscando espacio seguro…');
    plan = await discoverInstallPlan(loader);
    if (plan.mode === 'launcher') await loadApplication();
    else await loadFactory();
    connected = true;
    showPlan(plan);
    connectStep.classList.remove('is-current');
    connectStep.classList.add('is-complete');
    reviewStep.classList.add('is-current');
    connectButton.textContent = 'ESP32-S3 conectado';
    if (plan.mode === 'launcher') {
      const action = plan.action === 'update' ? 'actualización' : 'ranura nueva';
      setProgress(0, `M5Launcher detectado · ${action} en 0x${plan.target.offset.toString(16).toUpperCase()}`);
      appendLog(`M5Launcher preservado: ${plan.launcher.label} en 0x${plan.launcher.offset.toString(16).toUpperCase()}`);
      appendLog(`Destino CardGPT: 0x${plan.target.offset.toString(16).toUpperCase()} · ${plan.target.size / 1024} KiB`);
      appendLog('Borrado completo: desactivado');
    } else {
      setProgress(0, 'Sin M5Launcher compatible · instalación limpia disponible');
      appendLog(plan.reason);
      appendLog('La flash completa solo se borrará después de la confirmación explícita.');
    }
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

const askForConfirmation = (): Promise<boolean> => new Promise(resolve => {
  const onClose = (): void => {
    confirmDialog.removeEventListener('close', onClose);
    resolve(confirmDialog.returnValue === 'confirm');
  };
  confirmDialog.addEventListener('close', onClose);
  confirmDialog.returnValue = 'cancel';
  confirmDialog.showModal();
});

const phaseLabels = (): Record<InstallPhase, string> => ({
  validating: 'Validando de nuevo el dispositivo y el firmware…',
  'writing-app': 'Escribiendo CardGPT sin tocar los demás firmwares…',
  'verifying-app': 'Verificando CardGPT…',
  'writing-table': 'Registrando la nueva ranura en M5Launcher…',
  'verifying-table': 'Verificando la tabla de M5Launcher…',
  erasing: 'Borrando toda la flash. No desconectes el cable…',
  'writing-factory': 'Instalando la imagen completa de CardGPT…',
  'verifying-factory': 'Verificando los 8 MiB escritos…',
});

const flashFirmware = async (): Promise<void> => {
  if (!loader || !transport || !plan || !connected || !safetyCheck.checked || busy) return;
  const selectedPlan = plan;
  setBusy(true);
  if (!(await askForConfirmation())) { setBusy(false); return; }
  resultPanel.hidden = true;
  reviewStep.classList.remove('is-current');
  reviewStep.classList.add('is-complete');
  flashStep.classList.add('is-current');
  window.addEventListener('beforeunload', preventNavigation);
  try {
    const labels = phaseLabels();
    const onPhase = (phase: InstallPhase): void => {
      setIndeterminate(labels[phase]);
      appendLog(labels[phase]);
    };
    if (selectedPlan.mode === 'launcher') {
      await installIntoLauncher(
        loader,
        selectedPlan,
        await loadApplication(),
        onPhase,
        percent => setProgress(percent, 'Instalando CardGPT en M5Launcher…'),
      );
    } else {
      await installFactoryImage(
        loader,
        await loadFactory(),
        onPhase,
        percent => setProgress(percent, 'Instalando CardGPT desde cero…'),
      );
    }
    setIndeterminate('Reiniciando el Cardputer…');
    let restarted = true;
    try { await loader.after('hard_reset'); }
    catch { restarted = false; appendLog('Imagen verificada. Apaga y enciende el dispositivo manualmente.'); }
    setProgress(100, restarted ? 'Instalación verificada' : 'Instalación verificada · reinicia manualmente');
    flashStep.classList.remove('is-current');
    flashStep.classList.add('is-complete');
    resultPanel.hidden = false;
    appendLog('Instalación finalizada y verificada.');
    connected = false;
    try { await transport.disconnect(); } catch { /* Reiniciar puede cerrar el puerto primero. */ }
    loader = null;
    transport = null;
    plan = null;
    connectButton.textContent = 'Conectar por USB';
    safetyCheck.checked = false;
  } catch (error) {
    if (transport) {
      try { await transport.disconnect(); } catch { /* Puede haberse perdido el USB. */ }
    }
    resetConnectionUi();
    setError(error);
    appendLog(selectedPlan.mode === 'launcher'
      ? 'M5Launcher, NVS y los demás firmwares no fueron objetivos de escritura.'
      : 'Si el borrado llegó a iniciarse, reconecta en modo descarga y repite la instalación limpia.');
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
firmwareInfoButton.addEventListener('click', () => firmwareInfoDialog.showModal());

showDefaultCopy();
updateControls();
