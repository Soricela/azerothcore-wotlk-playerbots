-- Saved locally by WoW, separately for each character (see .toc).
local controller = CreateFrame("Frame")
local api, panel, rows, nameBox, selected, page, hint
local function database()
  AcoreWardrobeDB = AcoreWardrobeDB or {}
  AcoreWardrobeDB.outfits = AcoreWardrobeDB.outfits or {}
  return AcoreWardrobeDB.outfits
end
local function message(text) hint:SetText(text) end
local function cleanName()
  local name = string.match(nameBox:GetText() or "", "^%s*(.-)%s*$")
  if name == "" or #name > 48 or string.find(name, "[|%c]") then
    message("Nombre: 1–48 bytes, sin códigos de chat.")
    return nil
  end
  return name
end
local function exists(name, except)
  for i, outfit in ipairs(database()) do
    if i ~= except and string.lower(outfit.name) == string.lower(name) then return true end
  end
end
local function refresh()
  local data = database()
  page = math.max(1, math.min(page, math.max(1, math.ceil(#data / 8))))
  for i, row in ipairs(rows) do
    local index = (page - 1) * 8 + i
    local outfit = data[index]
    row.index = index
    if outfit then
      local shownName = outfit.name
      if #shownName > 34 then shownName = string.sub(shownName, 1, 31) .. "..." end
      row:SetText(string.format("%02d  %s%s", index, selected == index and "[Seleccionado] " or "", shownName))
      row:Show()
    else row:Hide() end
  end
  panel.previous:SetEnabled(page > 1)
  panel.next:SetEnabled(page * 8 < #data)
  panel.counter:SetText(string.format("%d / %d · %d conjuntos", page, math.max(1, math.ceil(#data / 8)), #data))
end
local function button(parent, text, x, y, width, callback)
  local b = CreateFrame("Button", nil, parent, "UIPanelButtonTemplate")
  b:SetSize(width, 25)
  b:SetPoint("TOPLEFT", x, y)
  b:SetText(text)
  b:SetScript("OnClick", callback)
  return b
end
local function create()
  api = AcoreWardrobeOutfitAPI
  local window = api.Window()
  page, rows = 1, {}
  panel = CreateFrame("Frame", nil, window)
  panel:SetSize(712, 570)
  panel:SetPoint("TOPLEFT", window, "TOPLEFT", 440, -84)
  panel:SetFrameLevel(window:GetFrameLevel() + 30)
  panel:EnableMouse(true)
  panel:SetBackdrop({bgFile="Interface\\Buttons\\WHITE8X8", edgeFile="Interface\\Tooltips\\UI-Tooltip-Border", edgeSize=16, insets={left=4,right=4,top=4,bottom=4}})
  panel:SetBackdropColor(0.035, 0.035, 0.045, 1)
  panel:SetScript("OnShow", function()
    api.SetCatalogHeaderVisible(false)
  end)
  panel:SetScript("OnHide", function()
    api.SetCatalogHeaderVisible(true)
  end)
  panel:Hide()
  local title = panel:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
  title:SetPoint("TOPLEFT", 18, -16)
  title:SetText("Mis conjuntos")
  local close = CreateFrame("Button", nil, panel, "UIPanelCloseButton")
  close:SetPoint("TOPRIGHT", -4, -4)
  local listLabel = panel:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
  listLabel:SetPoint("TOPLEFT", 20, -48)
  listLabel:SetText("Conjuntos guardados (selecciona uno)")
  for i=1,8 do
    local row = button(panel, "", 20, -68-(i-1)*31, 672, function(self)
      selected = self.index
      nameBox:SetText(database()[selected].name)
      nameBox:ClearFocus()
      refresh()
      message("Cargar prepara la vista previa; Aplicar confirma los cambios.")
    end)
    rows[i] = row
  end
  panel.previous = button(panel, "Anterior", 20, -328, 90, function() page=page-1; refresh() end)
  panel.next = button(panel, "Siguiente", 602, -328, 90, function() page=page+1; refresh() end)
  panel.counter = panel:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
  panel.counter:SetPoint("TOP", 0, -335)
  local nameLabel = panel:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
  nameLabel:SetPoint("TOPLEFT", 28, -364)
  nameLabel:SetText("Nombre para guardar o renombrar")
  nameBox = CreateFrame("EditBox", nil, panel)
  nameBox:SetSize(652, 28)
  nameBox:SetPoint("TOPLEFT", 28, -382)
  nameBox:SetBackdrop({
    bgFile="Interface\\DialogFrame\\UI-DialogBox-Background",
    edgeFile="Interface\\Tooltips\\UI-Tooltip-Border",
    edgeSize=12,
    insets={left=4,right=4,top=4,bottom=4},
  })
  nameBox:SetBackdropColor(0.03, 0.03, 0.03, 1)
  nameBox:SetFontObject(GameFontHighlight)
  nameBox:SetTextInsets(8, 8, 0, 0)
  nameBox:SetJustifyH("LEFT")
  nameBox:SetAutoFocus(false)
  nameBox:SetMaxLetters(48)
  nameBox:SetScript("OnEscapePressed", function(self) self:ClearFocus() end)
  nameBox:SetScript("OnEnterPressed", function(self) self:ClearFocus() end)
  button(panel, "Guardar como nuevo", 20, -425, 205, function()
    local name = cleanName()
    if not name then return end
    if exists(name) then message("Ya existe ese nombre. Usa otro para guardar una variante."); return end
    if #database() >= 100 then message("Límite: 100 conjuntos por personaje."); return end
    local pieces, err = api.Capture()
    if not pieces then message(err); return end
    table.insert(database(), {name=name, pieces=pieces})
    selected = #database(); page = math.ceil(selected / 8)
    nameBox:ClearFocus(); refresh()
    message("Guardado. Incluye las selecciones pendientes en ranuras equipadas.")
  end)
  button(panel, "Cargar vista previa", 253, -425, 205, function()
    local outfit = selected and database()[selected]
    if not outfit then message("Selecciona un conjunto."); return end
    local ok, err = api.Preview(outfit.pieces)
    if ok then panel:Hide() else message(err) end
  end)
  button(panel, "Renombrar", 487, -425, 205, function()
    if not selected or not database()[selected] then message("Selecciona un conjunto."); return end
    local name = cleanName()
    if not name then return end
    if exists(name, selected) then message("Ya existe ese nombre."); return end
    database()[selected].name = name
    nameBox:ClearFocus(); refresh(); message("Nombre actualizado.")
  end)
  StaticPopupDialogs.ACORE_WARDROBE_DELETE_OUTFIT = {
    text="¿Borrar el conjunto %s?", button1="Borrar", button2="Cancelar",
    OnAccept=function(self)
      for i, outfit in ipairs(database()) do
        if outfit == self.data then table.remove(database(), i); break end
      end
      selected=nil; refresh(); message("Conjunto borrado. El equipo conserva su apariencia.")
    end,
    timeout=0, whileDead=true, hideOnEscape=true,
  }
  button(panel, "Borrar…", 20, -464, 205, function()
    local outfit = selected and database()[selected]
    if not outfit then message("Selecciona un conjunto."); return end
    local dialog = StaticPopup_Show("ACORE_WARDROBE_DELETE_OUTFIT", outfit.name)
    if dialog then dialog.data=outfit end
  end)
  hint = panel:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
  hint:SetPoint("TOPLEFT", 20, -508); hint:SetWidth(672); hint:SetJustifyH("LEFT")
  message("Guarda el aspecto actual o carga uno para verlo antes de aplicar.")
  AcoreWardrobeOutfitPanel = {
    Show = function() panel:Show(); refresh() end,
    Hide = function() panel:Hide() end,
    IsShown = function() return panel:IsShown() end,
  }
end
controller:SetScript("OnUpdate", function(self)
  if AcoreWardrobeOutfitAPI and AcoreWardrobeOutfitAPI.Window() then
    create()
    self:SetScript("OnUpdate", nil)
  end
end)
