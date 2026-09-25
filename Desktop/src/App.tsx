import { Navigate, Route, Routes } from "react-router-dom";

import { IndexPage } from "@/pages/IndexPage";
import {
  CombinedPage,
  ControlsPage,
  DesktopPage,
  DockPage,
  InputLabPage,
  MenuBarPage,
  WindowPage,
} from "@/pages/LabPages";

export function App() {
  return (
    <Routes>
      <Route path="/" element={<IndexPage />} />
      <Route path="/desktop" element={<DesktopPage />} />
      <Route path="/window" element={<WindowPage />} />
      <Route path="/controls" element={<ControlsPage />} />
      <Route path="/dock" element={<DockPage />} />
      <Route path="/menubar" element={<MenuBarPage />} />
      <Route path="/input-lab" element={<InputLabPage />} />
      <Route path="/combined" element={<CombinedPage />} />
      <Route path="*" element={<Navigate to="/" replace />} />
    </Routes>
  );
}
