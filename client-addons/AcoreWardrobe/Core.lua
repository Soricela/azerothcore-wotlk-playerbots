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
local updateElapsed = 0
local retryElapsed = 0
local retryCount = 0
local requestSync

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
  if not name or not equipLoc or equipLoc == "" then
    unresolved[itemId] = true
    return false
  end

  catalog[itemId] = {
    id = itemId,
    name = name,
    link = link,
    quality = quality or 1,
    equipLoc = equipLoc,
    texture = texture,
  }
  unresolved[itemId] = nil
  return true
end

local function refreshGrid()
  if not addon.window then
    return
  end

  clear(filtered)
  local category = categories[currentCategory]
  local search = string.lower(trim(addon.window.search:GetText()))

  for _, item in pairs(catalog) do
    if itemMatchesCategory(item, category) and
        (search == "" or string.find(string.lower(item.name), search, 1, true)) then
      table.insert(filtered, item)
    end
  end

  table.sort(filtered, function(left, right)
    if left.name == right.name then
      return left.id < right.id
    end
    return left.name < right.name
  end)

  addon.window.pageText:SetFormattedText("Página %d%s", page, hasMorePages and " / …" or "")
  addon.window.countText:SetFormattedText("Apariencias: %d", #filtered)
  addon.window.previous:SetEnabled(page > 1)
  addon.window.next:SetEnabled(hasMorePages)

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
  for itemId in pairs(collection) do
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
  clear(catalog)
  clear(filtered)
  clear(unresolved)
  hasMorePages = false
  refreshGrid()
  if exploreMode then
    setStatus("Explorando apariencias de esta categoría...")
    queueCommand(string.format(".transmog wardrobe browse %d %d", currentSlot(), page - 1))
  else
    setStatus("Cargando apariencias compatibles...")
    queueCommand(string.format(".transmog wardrobe catalog %d %d", currentSlot(), page - 1))
  end
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
  button:SetPoint("TOPLEFT", parent, "TOPLEFT", 424 + column * 68, -180 - row * 68)

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
  frame:SetSize(920, 610)
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
  frame.model:SetSize(360, 455)
  frame.model:SetPoint("TOPLEFT", 28, -105)
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
    refreshGrid()
  end)

  local searchLabel = frame:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
  searchLabel:SetPoint("BOTTOMLEFT", frame.search, "TOPLEFT", 2, 4)
  searchLabel:SetText("Buscar en esta página")

  frame.countText = frame:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
  frame.countText:SetPoint("LEFT", frame.search, "RIGHT", 18, 0)

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

  frame.categoryButtons = {}
  for index, category in ipairs(categories) do
    local button = CreateFrame("Button", nil, frame, "UIPanelButtonTemplate")
    button:SetSize(104, 23)
    local column = (index - 1) % 7
    local row = math.floor((index - 1) / 7)
    button:SetPoint("TOPLEFT", 55 + column * 106, -48 - row * 25)
    button:SetText(category.label)
    button:SetScript("OnClick", function()
      selectCategory(index)
    end)
    frame.categoryButtons[index] = button
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
    clear(catalog)
    clear(filtered)
    clear(unresolved)
    hasMorePages = false
    refreshGrid()
    setStatus("Recibiendo apariencias...")
    return true
  end

  local catalogSlot, catalogPage, catalogPayload = string.match(message, "^WARDROBE_CATALOG:(%d+):(%d+):(.*)$")
  if catalogSlot then
    if tonumber(catalogSlot) ~= currentSlot() or tonumber(catalogPage) ~= page - 1 then
      return true
    end
    for itemId in string.gmatch(catalogPayload, "(%d+)") do
      collection[tonumber(itemId)] = true
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

  if updateElapsed >= 0.20 and #commandQueue > 0 then
    updateElapsed = 0
    SendChatMessage(table.remove(commandQueue, 1), "SAY")
  end

  if retryElapsed >= 1 and next(unresolved) and retryCount < 15 then
    retryElapsed = 0
    retryCount = retryCount + 1
    retryUnresolvedItems()
  end
end)

addon:RegisterEvent("PLAYER_LOGIN")
addon:RegisterEvent("PLAYER_EQUIPMENT_CHANGED")
addon:SetScript("OnEvent", function(_, event)
  if event == "PLAYER_LOGIN" then
    AcoreWardrobeDB = AcoreWardrobeDB or {}
    createWindow()
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
