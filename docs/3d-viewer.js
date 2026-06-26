import { ref, onMounted, onBeforeUnmount, watch, nextTick } from "vue";
import * as THREE from "three";
import { GLTFLoader } from "three/addons/loaders/GLTFLoader.js";
import { OrbitControls } from "three/addons/controls/OrbitControls.js";
import { RoomEnvironment } from "three/addons/environments/RoomEnvironment.js";

export const Pocket3DViewer = {
  props: {
    modelUrl: { type: String, default: "./WSEPAPER.glb" },
    screenMode: { type: String, default: "home" },
    screenText: { type: String, default: "Pocket Ink" },
    screenMaterialName: { type: String, default: "Material.Screen" },
    canvasBackground: { type: String, default: "#abbdc7" },
    sceneBackground: { type: String, default: "" },
    debugScreen: { type: Boolean, default: false },
    flipScreenSide: { type: Boolean, default: false },
  },

  template: `
    <div ref="containerRef" class="viewer" :style="viewerBg">
      <canvas ref="screenCanvasRef" class="screen-canvas"></canvas>
    </div>
  `,

  setup(props) {
    const containerRef = ref(null);
    const screenCanvasRef = ref(null);

    const viewerBg = ref({});

    let renderer = null;
    let scene = null;
    let camera = null;
    let controls = null;
    let model = null;
    let animationFrameId = null;
    let resizeObserver = null;
    let canvasTexture = null;
    let pmremGenerator = null;
    let environmentTexture = null;

    let screenOverlayMeshes = [];
    let activeScreenMode = ref("sleepClock");
    let modelReady = false;
    let introActive = false;
    let introStartTime = 0;
    let batteryPercent = 76;
    let batteryCharging = false;
    let batteryManager = null;
    let batteryListeners = [];
    let batteryInitCancelled = false;
    const introDuration = 1500;
    const introStartAngle = 80 * Math.PI / 180;
    const introRadius = 4.4;
    const cameraHeight = 0.55;
    const viewerPixelRatio = Math.min(window.devicePixelRatio || 1, 2);

    const SCREEN_LOGICAL_WIDTH = 200;
    const SCREEN_LOGICAL_HEIGHT = 200;
    const SCREEN_CANVAS_WIDTH = 1024;
    const SCREEN_CANVAS_HEIGHT = 1024;

    function getHomeScreenState() {
      const now = new Date();
      const dateText = now.toLocaleDateString("en-US", {
        weekday: "short",
        month: "short",
        day: "numeric",
      });
      const timeText = now.toLocaleTimeString("en-US", {
        hour: "2-digit",
        minute: "2-digit",
      });
      return {
        dateText,
        timeText,
        batteryPercent,
        charging: batteryCharging,
      };
    }

    function drawTextCenter(ctx, text, x, y, font, fillStyle = "#111827") {
      ctx.save();
      ctx.fillStyle = fillStyle;
      ctx.font = font;
      ctx.textAlign = "center";
      ctx.textBaseline = "alphabetic";
      ctx.fillText(text, x, y);
      ctx.restore();
    }

    function drawTextLeft(ctx, text, x, y, font, fillStyle = "#111827") {
      ctx.save();
      ctx.fillStyle = fillStyle;
      ctx.font = font;
      ctx.textAlign = "left";
      ctx.textBaseline = "alphabetic";
      ctx.fillText(text, x, y);
      ctx.restore();
    }

    function drawTextRight(ctx, text, x, y, font, fillStyle = "#111827") {
      ctx.save();
      ctx.fillStyle = fillStyle;
      ctx.font = font;
      ctx.textAlign = "right";
      ctx.textBaseline = "alphabetic";
      ctx.fillText(text, x, y);
      ctx.restore();
    }

    function batteryGlyphForPercentage(level) {
      if (level >= 90) return "B";
      if (level >= 65) return "C";
      if (level >= 35) return "D";
      return "E";
    }

    function batteryGlyphForState(state) {
      if (state.charging) return "A";
      return batteryGlyphForPercentage(state.batteryPercent);
    }

    function drawStatusBattery(ctx, x, y, state) {
      ctx.save();
      ctx.fillStyle = "#111827";
      ctx.globalAlpha = 0.8;
      ctx.font = '400 18px "IconASCII"';
      ctx.textAlign = "left";
      ctx.textBaseline = "alphabetic";
      ctx.fillText(batteryGlyphForState(state), x, y);
      ctx.restore();
    }

    function renderStatusBar(ctx, state) {
      ctx.save();
      ctx.fillStyle = props.canvasBackground;
      ctx.fillRect(0, 0, SCREEN_LOGICAL_WIDTH, 16);
      ctx.strokeStyle = "#111827";
      ctx.lineWidth = 1;
      ctx.beginPath();
      ctx.moveTo(0, 15.5);
      ctx.lineTo(SCREEN_LOGICAL_WIDTH, 15.5);
      ctx.stroke();
      drawTextLeft(ctx, state.dateText, 4, 11, '7.5px "IBM Plex Mono", monospace', "#111827");
      drawTextRight(ctx, state.timeText, 176, 11, '7.5px "IBM Plex Mono", monospace', "#111827");
      drawStatusBattery(ctx, 179, 13, state);
      ctx.restore();
    }

    function syncBatteryState(manager) {
      if (!manager || typeof manager.level !== "number") {
        batteryPercent = 76;
        batteryCharging = false;
        return;
      }

      batteryPercent = Math.max(0, Math.min(100, Math.round(manager.level * 100)));
      batteryCharging = !!manager.charging;
    }

    function refreshBatteryState() {
      syncBatteryState(batteryManager);
      if (activeScreenMode.value === "home") {
        paintScreenCanvas();
      }
    }

    async function initBatteryState() {
      if (!navigator || typeof navigator.getBattery !== "function") return;
      try {
        const manager = await navigator.getBattery();
        if (batteryInitCancelled) return;

        batteryManager = manager;
        batteryListeners = [];

        const onBatteryChange = () => refreshBatteryState();
        manager.addEventListener("levelchange", onBatteryChange);
        manager.addEventListener("chargingchange", onBatteryChange);
        batteryListeners.push(["levelchange", onBatteryChange], ["chargingchange", onBatteryChange]);

        refreshBatteryState();
      } catch (error) {
        console.warn("Battery Status API unavailable:", error);
      }
    }

    function measureTextWidth(ctx, text, font) {
      ctx.save();
      ctx.font = font;
      const width = ctx.measureText(text).width;
      ctx.restore();
      return width;
    }

    async function loadViewerFonts() {
      if (!document.fonts || !document.fonts.load) return;
      await Promise.all([
        document.fonts.load('400 22px Quantico'),
        document.fonts.load('400 18px "IconASCII"'),
        document.fonts.load('400 9px "IBM Plex Mono"'),
      ]);
    }

    function renderHomeScreen(ctx) {
      const state = getHomeScreenState();
      ctx.save();
      ctx.fillStyle = props.canvasBackground;
      ctx.fillRect(0, 0, SCREEN_LOGICAL_WIDTH, SCREEN_LOGICAL_HEIGHT);
      renderStatusBar(ctx, state);

      ctx.fillStyle = "#111827";
      ctx.textBaseline = "alphabetic";

      const logo = props.screenText || "Pocket Ink";
      const logoFont = '400 22px Quantico, "Space Grotesk", "Arial", sans-serif';
      const logoScale = 0.92;
      const logoWidth = measureTextWidth(ctx, logo, logoFont) * logoScale;
      const logoX = 100;
      const logoLeft = logoX - logoWidth / 2;
      ctx.save();
      ctx.translate(logoX, 0);
      ctx.scale(logoScale, 1);
      ctx.font = logoFont;
      ctx.textAlign = "center";
      ctx.fillText(logo, 0, 81);
      ctx.restore();

      const osFont = '400 9px "IBM Plex Mono", monospace';
      const osWidth = measureTextWidth(ctx, "os", osFont);
      const osX = logoLeft + logoWidth - osWidth + 0.25;
      drawTextLeft(ctx, "os", osX, 92, osFont, "#111827");

      const footerFont = '400 8px "IBM Plex Mono", monospace';
      drawTextCenter(ctx, "SUN: contact", 100, 182, footerFont, "#111827");
      drawTextCenter(ctx, "PWR: apps", 100, 192, footerFont, "#111827");

      ctx.restore();
    }

    function renderSleepClockScreen(ctx) {
      const state = getHomeScreenState();
      ctx.save();
      ctx.fillStyle = props.canvasBackground;
      ctx.fillRect(0, 0, SCREEN_LOGICAL_WIDTH, SCREEN_LOGICAL_HEIGHT);
      ctx.fillStyle = "#111827";
      ctx.textBaseline = "alphabetic";
      drawTextCenter(ctx, "S", 100, 42, '400 18px IconASCII, "IBM Plex Mono", monospace', "#111827");
      drawTextCenter(ctx, state.timeText, 100, 104, '400 28px Quantico, "Space Grotesk", "Arial", sans-serif', "#111827");
      drawTextCenter(ctx, state.dateText, 100, 140, '400 7px Quantico, "Space Grotesk", "Arial", sans-serif', "#111827");

      ctx.restore();
    }

    const screenRenderers = {
      home: renderHomeScreen,
      sleepClock: renderSleepClockScreen,
      lock: renderSleepClockScreen,
    };

    function paintScreenCanvas() {
      const canvas = screenCanvasRef.value;
      if (!canvas) return;
      canvas.width = SCREEN_CANVAS_WIDTH;
      canvas.height = SCREEN_CANVAS_HEIGHT;
      const ctx = canvas.getContext("2d");
      if (!ctx) return;
      ctx.imageSmoothingEnabled = true;
      const scaleX = SCREEN_CANVAS_WIDTH / SCREEN_LOGICAL_WIDTH;
      const scaleY = SCREEN_CANVAS_HEIGHT / SCREEN_LOGICAL_HEIGHT;
      ctx.setTransform(scaleX, 0, 0, scaleY, 0, 0);
      ctx.clearRect(0, 0, SCREEN_LOGICAL_WIDTH, SCREEN_LOGICAL_HEIGHT);
      const renderer = screenRenderers[activeScreenMode.value] || screenRenderers.home;
      renderer(ctx);
      if (canvasTexture) {
        canvasTexture.needsUpdate = true;
      }
    }

    function createCanvasTexture() {
      const canvas = screenCanvasRef.value;
      canvasTexture = new THREE.CanvasTexture(canvas);
      canvasTexture.flipY = false;
      canvasTexture.colorSpace = THREE.SRGBColorSpace;
      canvasTexture.magFilter = THREE.LinearFilter;
      canvasTexture.minFilter = THREE.LinearMipmapLinearFilter;
      canvasTexture.wrapS = THREE.ClampToEdgeWrapping;
      canvasTexture.wrapT = THREE.ClampToEdgeWrapping;
      canvasTexture.generateMipmaps = true;
      canvasTexture.needsUpdate = true;
    }

    function createScreenOverlayMaterial() {
      const opts = {
        side: props.flipScreenSide ? THREE.BackSide : THREE.FrontSide,
        depthTest: true,
        depthWrite: false,
        toneMapped: false,
        polygonOffset: true,
        polygonOffsetFactor: -4,
        polygonOffsetUnits: -4,
      };
      const material = props.debugScreen
        ? new THREE.MeshBasicMaterial({ ...opts, color: "#ff00ff" })
        : new THREE.MeshBasicMaterial({ ...opts, map: canvasTexture });
      material.name = "Generated.CanvasScreen";
      material.needsUpdate = true;
      return material;
    }

    function createScreenBaseMaterial() {
      const material = new THREE.MeshBasicMaterial({
        color: props.canvasBackground,
        side: THREE.FrontSide,
        depthTest: true,
        depthWrite: true,
        toneMapped: false,
      });
      material.name = props.screenMaterialName;
      material.needsUpdate = true;
      return material;
    }

    function getBestProjectionAxes(positions) {
      const min = [Infinity, Infinity, Infinity];
      const max = [-Infinity, -Infinity, -Infinity];
      for (let i = 0; i < positions.length; i += 3) {
        min[0] = Math.min(min[0], positions[i]);
        min[1] = Math.min(min[1], positions[i + 1]);
        min[2] = Math.min(min[2], positions[i + 2]);
        max[0] = Math.max(max[0], positions[i]);
        max[1] = Math.max(max[1], positions[i + 1]);
        max[2] = Math.max(max[2], positions[i + 2]);
      }
      const extents = [max[0] - min[0], max[1] - min[1], max[2] - min[2]];
      const axesByExtent = [0, 1, 2].sort((a, b) => extents[b] - extents[a]);
      return { uAxis: axesByExtent[0], vAxis: axesByExtent[1], min, max, extents };
    }

    function createGeneratedScreenGeometry(sourceGeometry, targetMaterialIndex) {
      const geometry = sourceGeometry;
      const indexAttr = geometry.index;
      const positionAttr = geometry.getAttribute("position");
      const normalAttr = geometry.getAttribute("normal");
      if (!positionAttr) return null;

      const positions = [];
      const normals = [];
      const groups = geometry.groups && geometry.groups.length
        ? geometry.groups
        : [{ start: 0, count: indexAttr ? indexAttr.count : positionAttr.count, materialIndex: 0 }];

      function pushVertex(sourceIndex) {
        positions.push(positionAttr.getX(sourceIndex), positionAttr.getY(sourceIndex), positionAttr.getZ(sourceIndex));
        if (normalAttr) {
          normals.push(normalAttr.getX(sourceIndex), normalAttr.getY(sourceIndex), normalAttr.getZ(sourceIndex));
        }
      }

      for (const group of groups) {
        if ((group.materialIndex || 0) !== targetMaterialIndex) continue;
        const end = group.start + group.count;
        for (let i = group.start; i < end; i += 3) {
          pushVertex(indexAttr ? indexAttr.getX(i) : i);
          pushVertex(indexAttr ? indexAttr.getX(i + 1) : i + 1);
          pushVertex(indexAttr ? indexAttr.getX(i + 2) : i + 2);
        }
      }

      if (positions.length === 0) return null;

      const projection = getBestProjectionAxes(positions);
      const { uAxis, vAxis, min, extents } = projection;
      const uvs = [];

      for (let i = 0; i < positions.length; i += 3) {
        const uExtent = extents[uAxis] || 1;
        const vExtent = extents[vAxis] || 1;
        const u0 = (positions[i + uAxis] - min[uAxis]) / uExtent;
        const v0 = 1 - ((positions[i + vAxis] - min[vAxis]) / vExtent);
        uvs.push(v0, 1 - u0);
      }

      const screenGeometry = new THREE.BufferGeometry();
      screenGeometry.setAttribute("position", new THREE.Float32BufferAttribute(positions, 3));
      screenGeometry.setAttribute("uv", new THREE.Float32BufferAttribute(uvs, 2));
      if (normals.length > 0) {
        screenGeometry.setAttribute("normal", new THREE.Float32BufferAttribute(normals, 3));
      } else {
        screenGeometry.computeVertexNormals();
      }
      screenGeometry.computeBoundingSphere();
      screenGeometry.computeBoundingBox();
      return screenGeometry;
    }

    function removeScreenOverlays() {
      screenOverlayMeshes.forEach((overlay) => {
        overlay.removeFromParent();
        if (overlay.geometry) overlay.geometry.dispose();
        const materials = Array.isArray(overlay.material) ? overlay.material : [overlay.material];
        materials.forEach((m) => { if (m) m.dispose(); });
      });
      screenOverlayMeshes = [];
    }

    function applyScreenTexture(root) {
      removeScreenOverlays();
      let found = false;
      const overlayJobs = [];

      root.traverse((obj) => {
        if (obj.userData && obj.userData.isCanvasScreenOverlay) return;
        if (!obj.isMesh || !obj.material || !obj.geometry) return;
        const materials = Array.isArray(obj.material) ? obj.material : [obj.material];
        const replacementMaterials = materials.slice();
        let changedThisMesh = false;

        materials.forEach((material, materialIndex) => {
          if (!material) return;
          if (material.name !== props.screenMaterialName) return;
          found = true;
          changedThisMesh = true;
          replacementMaterials[materialIndex] = createScreenBaseMaterial();
          overlayJobs.push({ sourceMesh: obj, materialIndex });
        });

        if (changedThisMesh) {
          obj.material = Array.isArray(obj.material) ? replacementMaterials : replacementMaterials[0];
          const finalMaterials = Array.isArray(obj.material) ? obj.material : [obj.material];
          finalMaterials.forEach((m) => { if (m) m.needsUpdate = true; });
        }
      });

      overlayJobs.forEach(({ sourceMesh, materialIndex }) => {
        const screenGeometry = createGeneratedScreenGeometry(sourceMesh.geometry, materialIndex);
        if (!screenGeometry) return;
        const overlay = new THREE.Mesh(screenGeometry, createScreenOverlayMaterial());
        overlay.name = `CanvasScreenOverlay_${sourceMesh.name || "mesh"}`;
        overlay.userData.isCanvasScreenOverlay = true;
        overlay.renderOrder = 50;
        overlay.frustumCulled = false;
        sourceMesh.add(overlay);
        screenOverlayMeshes.push(overlay);
      });
    }

    function improveImportedMaterials(root) {
      root.traverse((obj) => {
        if (!obj.isMesh || !obj.material) return;
        const materials = Array.isArray(obj.material) ? obj.material : [obj.material];
        materials.forEach((material) => {
          if (!material) return;
          if (material.name === props.screenMaterialName) return;
          if (material.name === "Generated.CanvasScreen") return;
          material.transparent = false;
          material.opacity = 1;
          material.depthWrite = true;
          material.depthTest = true;
          if ("envMapIntensity" in material) material.envMapIntensity = 1.2;
          if ("roughness" in material) material.roughness = Math.min(Math.max(material.roughness, 0.34), 0.84);
          if ("metalness" in material) material.metalness = Math.min(material.metalness, 0.35);
          material.needsUpdate = true;
        });
      });
    }

    function centerAndScaleModel(root) {
      root.updateWorldMatrix(true, true);
      const box = new THREE.Box3().setFromObject(root);
      const size = new THREE.Vector3();
      const center = new THREE.Vector3();
      box.getSize(size);
      box.getCenter(center);
      root.position.x -= center.x;
      root.position.y -= center.y;
      root.position.z -= center.z;
      const maxDimension = Math.max(size.x, size.y, size.z);
      if (maxDimension > 0) root.scale.setScalar(2.43 / maxDimension);
      root.position.y -= 0.3;
      root.rotation.y = Math.PI;
      root.updateWorldMatrix(true, true);
      if (controls) { controls.target.set(0, 0, 0); controls.update(); }
    }

    function disposeObject(root) {
      root.traverse((obj) => {
        if (!obj.isMesh) return;
        if (obj.geometry) obj.geometry.dispose();
        const materials = Array.isArray(obj.material) ? obj.material : [obj.material];
        materials.forEach((material) => {
          if (!material) return;
          for (const key in material) {
            const value = material[key];
            if (value && value !== canvasTexture && typeof value.dispose === "function") value.dispose();
          }
          material.dispose();
        });
      });
    }

    function clearModel() {
      removeScreenOverlays();
      if (!model || !scene) return;
      scene.remove(model);
      disposeObject(model);
      model = null;
    }

    function loadModel() {
      clearModel();
      modelReady = false;
      const loader = new GLTFLoader();
      loader.load(props.modelUrl, (gltf) => {
        model = gltf.scene;
        improveImportedMaterials(model);
        applyScreenTexture(model);
        centerAndScaleModel(model);
        scene.add(model);
        modelReady = true;
      }, undefined, (error) => console.error("Failed to load GLB model:", error));
    }

    function resizeRenderer() {
      if (!containerRef.value || !renderer || !camera) return;
      if (!containerRef.value.isConnected) return;
      const rect = containerRef.value.getBoundingClientRect();
      const width = Math.floor(rect.width);
      const height = Math.floor(rect.height);
      if (width <= 0 || height <= 0) return;
      renderer.setSize(width, height, false);
      camera.aspect = width / height;
      camera.updateProjectionMatrix();
    }

    function animate() {
      if (!renderer || !scene || !camera) return;
      animationFrameId = requestAnimationFrame(animate);
      if (introActive) {
        const elapsed = performance.now() - introStartTime;
        const t = Math.min(elapsed / introDuration, 1);
        const eased = 1 - Math.pow(1 - t, 3);
        const angle = introStartAngle * (1 - eased);
        camera.position.x = introRadius * Math.sin(angle);
        camera.position.z = introRadius * Math.cos(angle);
        if (t >= 1) introActive = false;
      }
      if (!introActive && modelReady && activeScreenMode.value !== (props.screenMode || "home")) {
        activeScreenMode.value = props.screenMode || "home";
        paintScreenCanvas();
      }
      if (controls) controls.update();
      renderer.render(scene, camera);
    }

    onMounted(async () => {
      await nextTick();
      const container = containerRef.value;
      if (!container) return;

      scene = new THREE.Scene();
      if (props.sceneBackground) {
        scene.background = new THREE.Color(props.sceneBackground);
      }

      camera = new THREE.PerspectiveCamera(45, 1, 0.1, 100);
      camera.position.set(
        introRadius * Math.sin(introStartAngle),
        cameraHeight,
        introRadius * Math.cos(introStartAngle),
      );

      renderer = new THREE.WebGLRenderer({
        antialias: true,
        alpha: !props.sceneBackground,
        powerPreference: "high-performance",
      });

      renderer.setPixelRatio(viewerPixelRatio);
      renderer.outputColorSpace = THREE.SRGBColorSpace;
      renderer.toneMapping = THREE.ACESFilmicToneMapping;
      renderer.toneMappingExposure = 1.12;

      container.appendChild(renderer.domElement);

      pmremGenerator = new THREE.PMREMGenerator(renderer);
      environmentTexture = pmremGenerator.fromScene(new RoomEnvironment(), 0.04).texture;
      scene.environment = environmentTexture;

      controls = new OrbitControls(camera, renderer.domElement);
      controls.enableDamping = true;
      controls.dampingFactor = 0.075;
      controls.enablePan = false;
      controls.enableZoom = true;
      controls.rotateSpeed = 0.65;
      controls.zoomSpeed = 0.75;
      controls.minDistance = 1.7;
      controls.maxDistance = 8;
      controls.target.set(0, 0, 0);
      controls.update();

      introActive = true;
      introStartTime = performance.now();

      const hemisphereLight = new THREE.HemisphereLight("#ffffff", "#8b95a1", 1.7);
      scene.add(hemisphereLight);
      const keyLight = new THREE.DirectionalLight("#ffffff", 2.6);
      keyLight.position.set(3.5, 4.5, 5.5);
      scene.add(keyLight);
      const fillLight = new THREE.DirectionalLight("#ffffff", 0.8);
      fillLight.position.set(-4, 2, 3);
      scene.add(fillLight);

      await loadViewerFonts();
      paintScreenCanvas();
      createCanvasTexture();
      void initBatteryState();
      loadModel();

      resizeObserver = new ResizeObserver(resizeRenderer);
      resizeObserver.observe(container);
      resizeRenderer();
      animate();
    });

    watch(() => [props.screenText, props.screenMode], () => {
      if (props.screenMode) {
        activeScreenMode.value = props.screenMode;
      }
      paintScreenCanvas();
    });

    watch(() => props.sceneBackground, (newColor) => {
      if (!scene) return;
      if (newColor) {
        scene.background = new THREE.Color(newColor);
      } else {
        scene.background = null;
      }
    });

    watch(() => [props.debugScreen, props.flipScreenSide], () => {
      if (model) applyScreenTexture(model);
    });

    onBeforeUnmount(() => {
      batteryInitCancelled = true;
      if (batteryManager) {
        batteryListeners.forEach(([eventName, handler]) => {
          batteryManager.removeEventListener(eventName, handler);
        });
        batteryListeners = [];
        batteryManager = null;
      }
      if (animationFrameId) { cancelAnimationFrame(animationFrameId); animationFrameId = null; }
      if (resizeObserver) {
        if (containerRef.value) resizeObserver.unobserve(containerRef.value);
        resizeObserver.disconnect();
        resizeObserver = null;
      }
      if (controls) { controls.dispose(); controls = null; }
      clearModel();
      if (canvasTexture) { canvasTexture.dispose(); canvasTexture = null; }
      if (environmentTexture) { environmentTexture.dispose(); environmentTexture = null; }
      if (pmremGenerator) { pmremGenerator.dispose(); pmremGenerator = null; }
      if (renderer) {
        if (renderer.domElement && renderer.domElement.parentNode) {
          renderer.domElement.parentNode.removeChild(renderer.domElement);
        }
        renderer.dispose();
        renderer = null;
      }
    });

    return { containerRef, screenCanvasRef, viewerBg };
  },
};
