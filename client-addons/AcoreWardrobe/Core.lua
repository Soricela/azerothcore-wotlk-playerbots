local addon = CreateFrame("Frame", "AcoreWardrobeController")

local categories = {
  { slot = 0,  label = "Cabeza",    types = { INVTYPE_HEAD = true } },
  { slot = 2,  label = "Hombros",   types = { INVTYPE_SHOULDER = true } },
  { slot = 14, label = "Espalda",   types = { INVTYPE_CLOAK = true } },
  { slot = 4,  label = "Pecho",     types = { INVTYPE_CHEST = true, INVTYPE_ROBE = true } },
  { slot = 18, label = "Tabardo",   types = { INVTYPE_TABARD = true } },
  { slot = 3,  label = "Camisa",    types = { INVTYPE_BODY = true } },
  { slot = 8,  label = "Muñecas",   types = { INVTYPE_WRIST = true } },
  { slot = 9,  label = "Manos",     types = { INVTYPE_HAND = true } },
  { slot = 5,  label = "Cintura",   types = { INVTYPE_WAIST = true } },
  { slot = 6,  label = "Piernas",   types = { INVTYPE_LEGS = true } },
  { slot = 7,  label = "Pies",      types = { INVTYPE_FEET = true } },
  { slot = 17, label = "Distancia", types = {
      INVTYPE_RANGED = true, INVTYPE_RANGEDRIGHT = true, INVTYPE_THROWN = true,
    } },
  { slot = 15, label = "Principal", types = {
      INVTYPE_WEAPON = true, INVTYPE_2HWEAPON = true, INVTYPE_WEAPONMAINHAND = true,
    } },
  { slot = 16, label = "Secundaria", types = {
      INVTYPE_WEAPON = true, INVTYPE_WEAPONOFFHAND = true, INVTYPE_SHIELD = true,
      INVTYPE_HOLDABLE = true,
    } },
}

local resultMessages = {
  [1] = "Apariencia aplicada.",
  [2] = "Ranura inválida.",
  [3] = "Apariencia inexistente.",
  [4] = "No se encontró el objeto de origen.",
  [5] = "Debes equipar un objeto en esa ranura.",
  [6] = "La apariencia no es compatible con el objeto equipado.",
  [7] = "No tienes suficiente dinero.",
  [8] = "No tienes los tokens necesarios.",
  [9] = "Apariencia eliminada.",
  [10] = "La ranura no tenía una apariencia aplicada.",
}

local collection = {}
local catalogOrder = {}
local active = {}
local pending = {}
local catalog = {}
local filtered = {}
local unresolved = {}
local commandQueue = {}
local currentCategory = 1
local page = 1
local pageSize = 30
local hasMorePages = false
local exploreMode = false
local qualityFilter = 0
local sortOrder = 0
local catalogTotal = 0
local updateElapsed = 0
local retryElapsed = 0
local retryCount = 0
local searchElapsed = 0
local searchPending = false
local requestSync
local refreshSlotButtons

local qualityLabels = {
  [0] = "Todas las calidades",
  [1] = "Común",
  [2] = "Poco común",
  [3] = "Rara",
  [4] = "Épica",
  [5] = "Legendaria",
  [6] = "Artefacto",
  [7] = "Reliquia",
}

local sortLabels = {
  [0] = "ID",
  [1] = "Nombre",
  [2] = "Calidad",
  [3] = "Nivel",
}

local function clear(target)
  for key in pairs(target) do
    target[key] = nil
  end
end

local function trim(value)
  return string.match(value or "", "^%s*(.-)%s*$")
end

local function currentSlot()
  return categories[currentCategory].slot
end

local function catalogSearchToken()
  local search = trim(addon.window and addon.window.search:GetText() or "")
  search = string.lower(search)
  search = string.gsub(search, "%s+", "+")
  search = string.gsub(search, "[^%w%+%-]", "")
  if search == "" then
    return "-"
  end
  return string.sub(search, 1, 32)
end

local function queueCommand(command)
  table.insert(commandQueue, command)
end

local function setStatus(message, red)
  if not addon.window then
    return
  end

  addon.window.status:SetText(message or "")
  if red then
    addon.window.status:SetTextColor(1, 0.25, 0.25)
  else
    addon.window.status:SetTextColor(0.4, 1, 0.4)
  end
end

local function itemMatchesCategory(item, category)
  return item.equipLoc and category.types[item.equipLoc]
end

local function refreshModel()
  local model = addon.window and addon.window.model
  if not model then
    return
  end

  model:SetUnit("player")
  if model.SetFacing then
    model:SetFacing(model.rotation or 0)
  end
  for slot, itemId in pairs(active) do
    local replacement = pending[slot]
    if replacement == nil then
      replacement = itemId
    end
    if replacement and replacement > 0 then
      local _, link = GetItemInfo(replacement)
      if link then
        model:TryOn(link)
      end
    end
  end

  for slot, itemId in pairs(pending) do
    if active[slot] == nil and itemId > 0 then
      local _, link = GetItemInfo(itemId)
      if link then
        model:TryOn(link)
      end
    elseif itemId == 0 and model.UndressSlot then
      model:UndressSlot(slot)
    end
  end
end

local function addCatalogItem(itemId)
  local name, link, quality, _, _, _, _, _, equipLoc, texture = GetItemInfo(itemId)

  catalog[itemId] = {
    id = itemId,
    name = name or ("Objeto " .. itemId),
    link = link or ("item:" .. itemId),
    quality = quality or 1,
    equipLoc = equipLoc,
    texture = texture,
  }
  if name then
    unresolved[itemId] = nil
    return true
  end

  -- The server has already filtered this page by slot.  Keep the button visible
  -- while the 3.3.5 client fetches item data instead of dropping it locally.
  unresolved[itemId] = true
  return false
end

local function refreshGrid()
  if not addon.window then
    return
  end

  clear(filtered)
  for _, itemId in ipairs(catalogOrder) do
    if catalog[itemId] then
      table.insert(filtered, catalog[itemId])
    end
  end

  local maxPage = math.max(1, math.ceil(catalogTotal / pageSize))
  addon.window.pageText:SetFormattedText("Página %d / %d", page, maxPage)
  addon.window.countText:SetFormattedText("Apariencias: %d / %d", #filtered, catalogTotal)
  addon.window.previous:SetEnabled(page > 1)
  addon.window.next:SetEnabled(hasMorePages)

  if refreshSlotButtons then
    refreshSlotButtons()
  end

  for index, button in ipairs(addon.window.itemButtons) do
    local item = filtered[index]
    button.item = item
    if item then
      button.icon:SetTexture(item.texture or "Interface\\Icons\\INV_Misc_QuestionMark")
      button:Show()
      if pending[currentSlot()] == item.id then
        button.selected:Show()
      else
        button.selected:Hide()
      end
    else
      button:Hide()
    end
  end
end

local function buildCatalog()
  clear(catalog)
  clear(unresolved)
  for _, itemId in ipairs(catalogOrder) do
    addCatalogItem(itemId)
  end
  retryCount = 0
  refreshGrid()
end

local function selectCategory(index)
  currentCategory = index
  page = 1
  for buttonIndex, button in ipairs(addon.window.categoryButtons) do
    if buttonIndex == index then
      button:LockHighlight()
    else
      button:UnlockHighlight()
    end
  end
  if addon.window:IsShown() then
    requestSync()
  else
    refreshGrid()
  end
end

local function selectItem(item)
  if not item then
    return
  end
  pending[currentSlot()] = item.id
  refreshModel()
  refreshGrid()
  setStatus("Cambio pendiente. Pulsa Aplicar para guardarlo.")
end

local function applyPending()
  local count = 0
  for slot, itemId in pairs(pending) do
    if itemId == 0 then
      queueCommand(string.format(".transmog wardrobe remove %d", slot))
    else
      queueCommand(string.format(".transmog wardrobe apply %d %d", slot, itemId))
    end
    count = count + 1
  end

  if count == 0 then
    setStatus("No hay cambios pendientes.", true)
  else
    setStatus("Aplicando apariencias...")
  end
end

local function cancelPending()
  clear(pending)
  refreshModel()
  refreshGrid()
  setStatus("Cambios cancelados.")
end

local function removeCurrentSlot()
  pending[currentSlot()] = 0
  refreshModel()
  refreshGrid()
  setStatus("La apariencia de esta ranura se eliminará al aplicar.")
end

local function requestCatalog(targetPage)
  page = math.max(1, targetPage or 1)
  clear(collection)
  clear(catalogOrder)
  clear(catalog)
  clear(filtered)
  clear(unresolved)
  hasMorePages = false
  catalogTotal = 0
  refreshGrid()
  local command = exploreMode and "browse" or "catalog"
  if exploreMode then
    setStatus("Explorando apariencias de esta categoría...")
  else
    setStatus("Cargando apariencias compatibles...")
  end
  queueCommand(string.format(".transmog wardrobe %s %d %d %d %d %s", command, currentSlot(), page - 1, qualityFilter, sortOrder, catalogSearchToken()))
end

requestSync = function()
  clear(active)
  requestCatalog(1)
end

local function createItemButton(parent, index)
  local button = CreateFrame("Button", nil, parent)
  button:SetSize(58, 58)
  local column = (index - 1) % 6
  local row = math.floor((index - 1) / 6)
  button:SetPoint("TOPLEFT", parent, "TOPLEFT", 424 + column * 68, -195 - row * 68)

  local background = button:CreateTexture(nil, "BACKGROUND")
  background:SetAllPoints()
  background:SetTexture("Interface\\Buttons\\UI-EmptySlot")

  button.icon = button:CreateTexture(nil, "ARTWORK")
  button.icon:SetPoint("TOPLEFT", 5, -5)
  button.icon:SetPoint("BOTTOMRIGHT", -5, 5)

  button.selected = button:CreateTexture(nil, "OVERLAY")
  button.selected:SetAllPoints()
  button.selected:SetTexture("Interface\\Buttons\\CheckButtonHilight")
  button.selected:SetBlendMode("ADD")
  button.selected:Hide()

  button:SetScript("OnClick", function(self)
    selectItem(self.item)
  end)
  button:SetScript("OnEnter", function(self)
    if not self.item then
      return
    end
    GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
    GameTooltip:SetHyperlink(self.item.link or ("item:" .. self.item.id))
    GameTooltip:Show()
  end)
  button:SetScript("OnLeave", function()
    GameTooltip:Hide()
  end)
  return button
end

local function createWindow()
  local frame = CreateFrame("Frame", "AcoreWardrobeFrame", UIParent)
  -- Leave a gap below the mouse-enabled DressUpModel.  Previously it overlapped
  -- the action buttons and captured their upper click area.
  frame:SetSize(920, 650)
  frame:SetPoint("CENTER")
  frame:SetFrameStrata("DIALOG")
  frame:SetMovable(true)
  frame:EnableMouse(true)
  frame:RegisterForDrag("LeftButton")
  frame:SetScript("OnDragStart", frame.StartMoving)
  frame:SetScript("OnDragStop", frame.StopMovingOrSizing)
  frame:SetBackdrop({
    bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background",
    edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Border",
    tile = true,
    tileSize = 32,
    edgeSize = 32,
    insets = { left = 11, right = 12, top = 12, bottom = 11 },
  })
  frame:Hide()

  local title = frame:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
  title:SetPoint("TOP", 0, -18)
  title:SetText("Wardrobe")

  local close = CreateFrame("Button", nil, frame, "UIPanelCloseButton")
  close:SetPoint("TOPRIGHT", -5, -5)

  frame.model = CreateFrame("DressUpModel", nil, frame)
  frame.model:SetSize(320, 455)
  frame.model:SetPoint("TOPLEFT", 58, -105)
  frame.model:SetUnit("player")
  frame.model:EnableMouse(true)
  frame.model.rotation = 0
  frame.model:SetScript("OnMouseDown", function(self, button)
    if button == "LeftButton" then
      self.rotating = true
      self.lastCursorX = GetCursorPosition()
    end
  end)
  frame.model:SetScript("OnMouseUp", function(self)
    self.rotating = false
  end)
  frame.model:SetScript("OnUpdate", function(self)
    if not self.rotating then
      return
    end

    local cursorX = GetCursorPosition()
    local delta = cursorX - (self.lastCursorX or cursorX)
    if delta ~= 0 then
      self.rotation = (self.rotation or 0) - delta * 0.01
      self:SetFacing(self.rotation)
      self.lastCursorX = cursorX
    end
  end)

  local modelBorder = CreateFrame("Frame", nil, frame)
  modelBorder:SetPoint("TOPLEFT", frame.model, -5, 5)
  modelBorder:SetPoint("BOTTOMRIGHT", frame.model, 5, -5)
  modelBorder:SetBackdrop({ edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border", edgeSize = 16 })

  frame.search = CreateFrame("EditBox", nil, frame, "InputBoxTemplate")
  frame.search:SetSize(240, 24)
  frame.search:SetPoint("TOPLEFT", 425, -135)
  frame.search:SetAutoFocus(false)
  frame.search:SetScript("OnTextChanged", function()
    page = 1
    searchElapsed = 0
    searchPending = true
    setStatus("Preparando búsqueda...")
  end)

  local searchLabel = frame:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
  searchLabel:SetPoint("BOTTOMLEFT", frame.search, "TOPLEFT", 2, 4)
  searchLabel:SetText("Buscar en esta página")

  frame.countText = frame:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
  frame.countText:SetPoint("LEFT", frame.search, "RIGHT", 18, 0)

  local qualityButton = CreateFrame("Button", nil, frame, "UIPanelButtonTemplate")
  qualityButton:SetSize(150, 22)
  qualityButton:SetPoint("TOPLEFT", 425, -164)
  qualityButton:SetText("Calidad: " .. qualityLabels[qualityFilter])
  qualityButton:SetScript("OnClick", function(self)
    qualityFilter = (qualityFilter + 1) % 8
    self:SetText("Calidad: " .. qualityLabels[qualityFilter])
    requestCatalog(1)
  end)
  frame.qualityButton = qualityButton

  local sortButton = CreateFrame("Button", nil, frame, "UIPanelButtonTemplate")
  sortButton:SetSize(120, 22)
  sortButton:SetPoint("LEFT", qualityButton, "RIGHT", 8, 0)
  sortButton:SetText("Orden: " .. sortLabels[sortOrder])
  sortButton:SetScript("OnClick", function(self)
    sortOrder = (sortOrder + 1) % 4
    self:SetText("Orden: " .. sortLabels[sortOrder])
    requestCatalog(1)
  end)
  frame.sortButton = sortButton

  local modeButton = CreateFrame("Button", nil, frame, "UIPanelButtonTemplate")
  modeButton:SetSize(180, 24)
  modeButton:SetPoint("TOPLEFT", 425, -105)
  modeButton:SetText("Modo: compatibles")
  modeButton:SetScript("OnClick", function(self)
    exploreMode = not exploreMode
    if exploreMode then
      self:SetText("Modo: explorar")
    else
      self:SetText("Modo: compatibles")
    end
    requestCatalog(1)
  end)
  frame.modeButton = modeButton

  -- CoA-style equipment slots.  They select the catalogue category while
  -- displaying either the pending/current transmog or the equipped item.
  frame.categoryButtons = {}
  for index, category in ipairs(categories) do
    local button = CreateFrame("Button", nil, frame)
    button:SetSize(40, 40)

    local x, y
    if index <= 9 then
      x = 18
      y = -118 - (index - 1) * 45
    else
      x = 378
      y = -118 - (index - 10) * 45
    end
    button:SetPoint("TOPLEFT", x, y)

    button.background = button:CreateTexture(nil, "BACKGROUND")
    button.background:SetAllPoints()
    button.background:SetTexture("Interface\\Buttons\\UI-Quickslot2")

    button.icon = button:CreateTexture(nil, "ARTWORK")
    button.icon:SetPoint("TOPLEFT", 5, -5)
    button.icon:SetPoint("BOTTOMRIGHT", -5, 5)

    button.selected = button:CreateTexture(nil, "OVERLAY")
    button.selected:SetTexture("Interface\\Buttons\\CheckButtonHilight")
    button.selected:SetBlendMode("ADD")
    button.selected:SetAllPoints()
    button.selected:Hide()

    button.pending = button:CreateTexture(nil, "OVERLAY")
    button.pending:SetTexture("Interface\\Buttons\\UI-CheckBox-Check")
    button.pending:SetSize(22, 22)
    button.pending:SetPoint("BOTTOMRIGHT", 5, -5)
    button.pending:Hide()

    button:SetScript("OnClick", function()
      selectCategory(index)
    end)
    button:SetScript("OnEnter", function(self)
      GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
      local appearanceId = pending[category.slot] or active[category.slot]
      local link = appearanceId and select(2, GetItemInfo(appearanceId))
      if not link then
        link = GetInventoryItemLink("player", category.slot + 1)
      end
      if link then
        GameTooltip:SetHyperlink(link)
      else
        GameTooltip:SetText(category.label)
        GameTooltip:AddLine("Seleccionar ranura", 0.7, 0.7, 0.7)
      end
      GameTooltip:Show()
    end)
    button:SetScript("OnLeave", function()
      GameTooltip:Hide()
    end)
    frame.categoryButtons[index] = button
  end

  refreshSlotButtons = function()
    if not addon.window then
      return
    end

    for index, category in ipairs(categories) do
      local button = addon.window.categoryButtons[index]
      local appearanceId = pending[category.slot]
      if appearanceId == nil then
        appearanceId = active[category.slot]
      end

      local texture
      if appearanceId == 0 then
        button.icon:SetTexture(nil)
      else
        if appearanceId and appearanceId > 0 then
          texture = select(10, GetItemInfo(appearanceId))
        end
        texture = texture or GetInventoryItemTexture("player", category.slot + 1)
        button.icon:SetTexture(texture or "Interface\\Icons\\INV_Misc_QuestionMark")
      end

      if index == currentCategory then
        button.selected:Show()
      else
        button.selected:Hide()
      end
      if pending[category.slot] ~= nil then
        button.pending:Show()
      else
        button.pending:Hide()
      end
    end
  end

  frame.itemButtons = {}
  for index = 1, pageSize do
    frame.itemButtons[index] = createItemButton(frame, index)
  end

  local previous = CreateFrame("Button", nil, frame, "UIPanelButtonTemplate")
  previous:SetSize(80, 24)
  previous:SetPoint("BOTTOMRIGHT", -224, 54)
  previous:SetText("Anterior")
  previous:SetScript("OnClick", function()
    if page > 1 then
      requestCatalog(page - 1)
    end
  end)
  frame.previous = previous

  local nextButton = CreateFrame("Button", nil, frame, "UIPanelButtonTemplate")
  nextButton:SetSize(80, 24)
  nextButton:SetPoint("BOTTOMRIGHT", -42, 54)
  nextButton:SetText("Siguiente")
  nextButton:SetScript("OnClick", function()
    if hasMorePages then
      requestCatalog(page + 1)
    end
  end)
  frame.next = nextButton

  frame.pageText = frame:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
  frame.pageText:SetPoint("CENTER", previous, "RIGHT", 51, 0)

  local apply = CreateFrame("Button", nil, frame, "UIPanelButtonTemplate")
  apply:SetSize(110, 28)
  apply:SetPoint("BOTTOMLEFT", 40, 48)
  apply:SetText("Aplicar")
  apply:SetScript("OnClick", applyPending)

  local cancel = CreateFrame("Button", nil, frame, "UIPanelButtonTemplate")
  cancel:SetSize(110, 28)
  cancel:SetPoint("LEFT", apply, "RIGHT", 8, 0)
  cancel:SetText("Cancelar")
  cancel:SetScript("OnClick", cancelPending)

  local remove = CreateFrame("Button", nil, frame, "UIPanelButtonTemplate")
  remove:SetSize(110, 28)
  remove:SetPoint("LEFT", cancel, "RIGHT", 8, 0)
  remove:SetText("Quitar")
  remove:SetScript("OnClick", removeCurrentSlot)

  frame.status = frame:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
  frame.status:SetPoint("BOTTOMLEFT", 40, 24)
  frame.status:SetPoint("BOTTOMRIGHT", -40, 24)
  frame.status:SetJustifyH("LEFT")

  frame:SetScript("OnShow", function()
    refreshModel()
    requestSync()
  end)

  addon.window = frame
  selectCategory(1)
end

local function processProtocol(message)
  local syncBeginSlot = string.match(message, "^WARDROBE_SYNC_BEGIN:(%d+)$")
  if syncBeginSlot then
    if tonumber(syncBeginSlot) ~= currentSlot() then
      return true
    end
    clear(active)
    setStatus("Recibiendo equipo...")
    return true
  end

  local syncEndSlot = string.match(message, "^WARDROBE_SYNC_END:(%d+)$")
  if syncEndSlot then
    if tonumber(syncEndSlot) ~= currentSlot() then
      return true
    end
    refreshModel()
    return true
  end

  local activeSlot, activeItem = string.match(message, "^WARDROBE_ACTIVE:(%d+):(%d+)$")
  if activeSlot then
    active[tonumber(activeSlot)] = tonumber(activeItem)
    return true
  end

  local catalogBeginSlot, catalogBeginPage = string.match(message, "^WARDROBE_CATALOG_BEGIN:(%d+):(%d+)$")
  if catalogBeginSlot then
    if tonumber(catalogBeginSlot) ~= currentSlot() or tonumber(catalogBeginPage) ~= page - 1 then
      return true
    end
    clear(collection)
    clear(catalogOrder)
    clear(catalog)
    clear(filtered)
    clear(unresolved)
    hasMorePages = false
    catalogTotal = 0
    refreshGrid()
    setStatus("Recibiendo apariencias...")
    return true
  end

  local totalSlot, totalPage, totalCount = string.match(message, "^WARDROBE_CATALOG_TOTAL:(%d+):(%d+):(%d+)$")
  if totalSlot then
    if tonumber(totalSlot) ~= currentSlot() or tonumber(totalPage) ~= page - 1 then
      return true
    end
    catalogTotal = tonumber(totalCount) or 0
    refreshGrid()
    return true
  end

  local catalogSlot, catalogPage, catalogPayload = string.match(message, "^WARDROBE_CATALOG:(%d+):(%d+):(.*)$")
  if catalogSlot then
    if tonumber(catalogSlot) ~= currentSlot() or tonumber(catalogPage) ~= page - 1 then
      return true
    end
    for itemId in string.gmatch(catalogPayload, "(%d+)") do
      itemId = tonumber(itemId)
      if not collection[itemId] then
        collection[itemId] = true
        table.insert(catalogOrder, itemId)
      end
    end
    return true
  end

  local catalogEndSlot, catalogEndPage, catalogMore = string.match(message, "^WARDROBE_CATALOG_END:(%d+):(%d+):(%d+)$")
  if catalogEndSlot then
    if tonumber(catalogEndSlot) ~= currentSlot() or tonumber(catalogEndPage) ~= page - 1 then
      return true
    end
    hasMorePages = tonumber(catalogMore) == 1
    buildCatalog()
    if next(collection) then
      if exploreMode then
        setStatus("Apariencias para explorar cargadas.")
      else
        setStatus("Apariencias compatibles cargadas.")
      end
    elseif exploreMode then
      setStatus("No hay apariencias para esta categoría.", true)
    else
      setStatus("Equipa un objeto en esta ranura.", true)
    end
    return true
  end

  local resultSlot, resultCode = string.match(message, "^WARDROBE_RESULT:(%d+):(.+)$")
  if resultSlot then
    local slot = tonumber(resultSlot)
    local numericCode = tonumber(resultCode)
    if numericCode == 1 then
      active[slot] = pending[slot]
      pending[slot] = nil
    elseif numericCode == 9 then
      active[slot] = nil
      pending[slot] = nil
    end

    local failed = numericCode ~= 1 and numericCode ~= 9
    setStatus(resultMessages[numericCode] or ("Resultado del servidor: " .. resultCode), failed)
    refreshModel()
    refreshGrid()
    return true
  end

  return false
end

local function chatFilter(_, _, message, ...)
  if processProtocol(message) then
    return true
  end
  return false, message, ...
end

local function retryUnresolvedItems()
  local changed = false
  for itemId in pairs(unresolved) do
    if addCatalogItem(itemId) then
      changed = true
    end
  end
  if changed then
    refreshGrid()
  end
end

addon:SetScript("OnUpdate", function(_, elapsed)
  updateElapsed = updateElapsed + elapsed
  retryElapsed = retryElapsed + elapsed
  if searchPending then
    searchElapsed = searchElapsed + elapsed
  end

  if updateElapsed >= 0.20 and #commandQueue > 0 then
    updateElapsed = 0
    SendChatMessage(table.remove(commandQueue, 1), "SAY")
  end

  if retryElapsed >= 1 and next(unresolved) and retryCount < 15 then
    retryElapsed = 0
    retryCount = retryCount + 1
    retryUnresolvedItems()
  end

  if searchPending and searchElapsed >= 0.45 then
    searchPending = false
    requestCatalog(1)
  end
end)

addon:RegisterEvent("PLAYER_LOGIN")
addon:RegisterEvent("PLAYER_EQUIPMENT_CHANGED")
addon:RegisterEvent("GET_ITEM_INFO_RECEIVED")
addon:SetScript("OnEvent", function(_, event, itemId, success)
  if event == "PLAYER_LOGIN" then
    AcoreWardrobeDB = AcoreWardrobeDB or {}
    createWindow()
  elseif event == "GET_ITEM_INFO_RECEIVED" then
    -- GetItemInfo() is asynchronous on a cold 3.3.5 client cache.  Update the
    -- affected button as soon as the client finishes its item query instead of
    -- waiting for the next retry tick.
    if success ~= false and itemId and unresolved[itemId] and catalog[itemId] then
      if addCatalogItem(itemId) then
        refreshGrid()
      end
    end
    if refreshSlotButtons then
      refreshSlotButtons()
    end
  elseif addon.window and addon.window:IsShown() then
    refreshModel()
    requestSync()
  end
end)

ChatFrame_AddMessageEventFilter("CHAT_MSG_SYSTEM", chatFilter)

SLASH_ACOREWARDROBE1 = "/wardrobe"
SLASH_ACOREWARDROBE2 = "/armario"
SlashCmdList.ACOREWARDROBE = function()
  if not addon.window then
    return
  end
  if addon.window:IsShown() then
    addon.window:Hide()
  else
    addon.window:Show()
  end
end
