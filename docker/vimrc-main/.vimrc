"==========================
" Handle .tpp file
"==========================

augroup CppTpp
    autocmd!
    autocmd BufRead,BufNewFile *.tpp set filetype=cpp
augroup END

"==========================
" Plugin Manager : vim-plug
"==========================

call plug#begin('~/.vim/plugged')

" --- LSP Support ---
Plug 'prabirshrestha/vim-lsp'
Plug 'mattn/vim-lsp-settings'
Plug 'prabirshrestha/asyncomplete.vim'
Plug 'prabirshrestha/asyncomplete-lsp.vim'
Plug 'habamax/vim-habanight'

call plug#end()

"==========================
" LSP + Asyncomplete Configuration (stable)
"==========================
if executable('clangd')

    " --- LSP (clangd) ---
    augroup LspSetup
        autocmd!
        autocmd User lsp_setup call lsp#register_server({
            \ 'name': 'clangd',
            \ 'cmd': ['clangd'],
            \ 'allowlist': ['c', 'cpp', 'objc', 'objcpp', 'hpp', 'tpp'],
            \ })
    augroup END

let g:lsp_diagnostics_enabled = 1
let g:lsp_diagnostics_highlights_enabled = 0
let g:lsp_diagnostics_signs_enabled = 0
let g:lsp_diagnostics_virtual_text_enabled = 0
let g:lsp_diagnostics_echo_cursor = 1

    " --- Asyncomplete ---
    augroup AsyncompleteSetup
        autocmd!
        autocmd User asyncomplete_setup call asyncomplete#register_source({
            \ 'name': 'clangd-lsp',
            \ 'allowlist': ['c', 'cpp', 'tpp'],
            \ 'completor': function('asyncomplete#sources#lsp#completor'),
            \ })
    augroup END
	
endif

" ===== Interface =====

set number
set relativenumber
set showmatch               " Mettre en évidence les parenthèses correspondantes
set termguicolors
set t_Co=256
syntax enable
syntax on " Coloration syntaxique
try
  colorscheme habanight
catch /E185/
endtry

" ===== Indentation =====
set smartindent             " Indentation intelligente"  set autoindent              " Indentation automatique

" ===== Recherche =====
set smartcase               " ... sauf si la recherche contient des majuscules
set hlsearch                " Surligner les résultats
set ignorecase              " Ignorer la casse lors des recherches
set incsearch               " Afficher les résultats au fur et à mesure de la frappe

" ===== Confort =====
set mouse=a                 " Activer la souris
set wildmenu                " Autocomplétion dans la ligne de commande
set clipboard=unnamedplus   " Partager le presse-papiers avec le système
set history=1000            " Historique plus long
set splitbelow              " Les splits horizontaux s'ouvrent en dessous
set splitright              " Les splits verticaux s'ouvrent à droite

" ===== Sauvegarde =====
set undofile                " Activer l'historique persistant
set undodir=~/.vim/undodir  " Dossier pour l'historique
silent! call mkdir(&undodir, "p")

nnoremap <SPACE> :nohlsearch<CR> " Espace pour enlever le surlignage

" ===== Explorateur de fichier =====
nnoremap <C-e> :set nosplitright<CR>:Vexplore<CR>:set splitright<CR>
nnoremap <C-q> :if &filetype ==# 'netrw' \| q \| else \| echo "Ce n'est pas netrw !" \| endif<CR>

" ===== Descendre monter de ligne =====
nnoremap <C-j> :m .+1<CR>==
nnoremap <C-k> :m .-2<CR>==
vnoremap <C-j> :m '>+1<CR>gv=gv
vnoremap <C-k> :m '<-2<CR>gv=gv

" === Couleurs du menu de complétion ===
highlight Pmenu      guibg=#1e1e1e guifg=#c0c0c0
highlight PmenuSel   guibg=#3c3c3c guifg=#ffffff gui=bold
highlight PmenuSbar  guibg=#2c2c2c
highlight PmenuThumb guibg=#5c5c5c

" (facultatif, si supporté par ton LSP / asyncomplete)
highlight PmenuKind  guifg=#61afef guibg=#1e1e1e
highlight PmenuExtra guifg=#888888 guibg=#1e1e1e
set pumheight=8
inoremap <expr> <CR> pumvisible() 
      \ ? (complete_info()['selected'] == -1 ? "\<C-g>u\<CR>" : "\<C-y>") 
      \ : "\<CR>"

