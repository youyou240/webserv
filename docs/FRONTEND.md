# Le côté navigateur — HTML, CSS et JavaScript

> Tout le reste de ce projet décrit **le serveur** : comment il lit des octets, les parse, et en renvoie d'autres. Ce document décrit **l'autre bout du fil** : ce que le navigateur *fait* de ces octets. C'est la partie qui n'est pas notée, mais sans laquelle on ne comprend ni pourquoi `Content-Type` compte, ni pourquoi une page déclenche plusieurs requêtes, ni ce que teste réellement `www/index.html`.
>
> Document écrit pour quelqu'un qui n'a jamais fait de web. Les analogies sont prises en C.

---

## 1. Trois langages, trois rôles

Quand tu ouvres `http://localhost:8080/`, ton serveur renvoie du **texte**. Ce texte peut être de trois natures, et c'est **l'en-tête `Content-Type` que tu émets** qui dit au navigateur laquelle :

| Langage | Rôle | `Content-Type` émis par webserv |
|---|---|---|
| **HTML** | la **structure** : quels blocs, dans quel ordre, quel texte | `text/html` |
| **CSS** | la **présentation** : couleurs, tailles, espacements | `text/css` |
| **JavaScript** | le **comportement** : réagir à un clic, envoyer une requête | `application/javascript` |

Analogie : si une page était un programme C, le HTML serait les `struct` (les données et leur imbrication), le CSS un fichier de configuration d'affichage, et le JavaScript les fonctions qui réagissent aux événements.

**Le point qui te concerne directement** : si `Router/MimeTypes` renvoyait `text/plain` pour `/style.css`, le navigateur **refuserait d'appliquer le style** — il afficherait le fichier comme du texte brut. Le MIME n'est pas décoratif, c'est une instruction. Cf. `ROUTING.md`.

---

## 2. HTML — la structure

### 2.1 Une balise

L'unité de base est la **balise** (*tag*) :

```html
<p>Bonjour</p>
 └┬┘       └─┬─┘
ouvrante   fermante
```

- `<p>` ouvre, `</p>` ferme (le `/` marque la fermeture).
- Entre les deux : le **contenu**.
- L'ensemble s'appelle un **élément**.

Certaines balises n'ont **pas de contenu**, donc pas de fermeture. On les appelle *void elements* :

```html
<meta charset="utf-8">
<link rel="stylesheet" href="/style.css">
<input id="file" type="file" name="file">
```

Il n'existe **pas** de `</meta>` ni de `</input>`. C'est une liste fermée : `meta`, `link`, `input`, `img`, `br`, `hr`.

### 2.2 Les attributs

Un attribut est une paire `nom="valeur"` dans la balise ouvrante :

```html
<a href="/about.html">About this server</a>
   └─┬─┘ └────┬─────┘ └────────┬────────┘
  nom      valeur           contenu
```

En C, tu écrirais quelque chose comme :

```c
typedef struct s_element {
	char			*tag;        /* "a" */
	char			*href;       /* "/about.html" */
	char			*text;       /* "About this server" */
	struct s_element	**children;
}	t_element;
```

Un attribut, c'est un **champ**. Une balise, c'est un **type de nœud**.

### 2.3 L'imbrication : un arbre

Les éléments s'emboîtent, jamais ne se croisent :

```html
<ul>
	<li><a href="/about.html">About</a></li>
	<li><a href="/style.css">Style</a></li>
</ul>
```

```
ul
├── li
│   └── a  ("About")
└── li
    └── a  ("Style")
```

Cet arbre porte un nom : le **DOM** (*Document Object Model*). C'est la représentation en mémoire que le navigateur construit après avoir parsé ton HTML — l'équivalent de ce que fait `HttpRequest::append` avec les octets d'une requête, mais pour un document.

**Interdit de croiser** : `<b><i>texte</b></i>` est invalide. Comme des accolades en C, ça s'imbrique proprement ou pas du tout.

### 2.4 Le squelette obligatoire

```html
<!DOCTYPE html>
<html lang="en">
<head>
	<meta charset="utf-8">
	<meta name="viewport" content="width=device-width, initial-scale=1">
	<title>webserv - test dashboard</title>
	<link rel="stylesheet" href="/style.css">
</head>
<body>
	...
</body>
</html>
```

| Ligne | Rôle |
|---|---|
| `<!DOCTYPE html>` | dit au navigateur « HTML moderne, mode standard ». Sans lui, il bascule en *quirks mode* et imite des bugs d'Internet Explorer des années 90. Toujours en première ligne. |
| `<html lang="en">` | la racine de l'arbre. `lang` sert aux lecteurs d'écran et aux correcteurs orthographiques. |
| `<head>` | les **métadonnées** : rien de visible. Ce que le navigateur doit savoir **avant** d'afficher. |
| `<meta charset="utf-8">` | l'encodage. **Doit être dans les 1024 premiers octets**, sinon le navigateur a déjà commencé à décoder de travers. C'est pour ça qu'il est en tout premier dans le `<head>`. |
| `<meta name="viewport" ...>` | sur mobile : « n'essaie pas de simuler un écran de 980px, utilise la vraie largeur ». |
| `<title>` | le texte de l'onglet. |
| `<link rel="stylesheet" href="/style.css">` | **déclenche une seconde requête HTTP** — voir §3. |
| `<body>` | tout ce qui est visible. |

### 2.5 Les balises utilisées dans `www/index.html`

Elles sont dites **sémantiques** : leur nom décrit *ce que la chose est*, pas *à quoi elle ressemble*.

| Balise | Signification | Où dans ta page |
|---|---|---|
| `<header>` | en-tête de page ou de section | le titre `webserv` |
| `<main>` | le contenu principal, unique dans la page | tout le tableau de bord |
| `<section>` | un bloc thématique, normalement introduit par un titre | une par feature testée |
| `<footer>` | pied de page | `Served from ./www/index.html` |
| `<h1>` … `<h6>` | titres, du plus important au moins | `<h1>webserv</h1>`, `<h2>Upload (POST)</h2>` |
| `<p>` | paragraphe de texte | partout |
| `<ul>` / `<li>` | liste **non ordonnée** / un élément de liste | les listes de liens |
| `<a href="…">` | un lien (*anchor*) | tous les liens de test |
| `<code>` | du texte qui est du code | `` `text/html` `` |
| `<strong>` | importance forte (rendu en gras) | `201 Created` |
| `<span>` | conteneur **en ligne**, sans signification propre | les badges de code HTTP |
| `<form>` | un formulaire — voir §6 | upload, 405, CGI POST |
| `<label>` | l'étiquette d'un champ de saisie | `Pick a file` |
| `<input>` | un champ de saisie | fichier, texte |
| `<button>` | un bouton | `POST /uploads` |
| `<output>` | zone destinée à un résultat calculé | le `HTTP 204` du DELETE |
| `<script>` | du JavaScript — voir §5 | le bloc DELETE en bas |

Règle : **on ne choisit pas une balise pour son apparence**. `<h2>` n'est pas « du gros texte », c'est « un titre de niveau 2 » — l'apparence, c'est le rôle du CSS. Si tu veux du gras sans importance sémantique, tu prends `<span>` et tu le stylises.

### 2.6 `aria-labelledby` et `id`

```html
<section aria-labelledby="upload">
	<h2 id="upload">Upload (POST)</h2>
```

- `id` : un **identifiant unique dans toute la page**. Sert d'ancre.
- `aria-labelledby="upload"` : dit à un lecteur d'écran « le nom de cette section est l'élément dont l'`id` vaut `upload` ». Sans ça, un utilisateur aveugle qui navigue de section en section entendrait « section », « section », « section ».

`id` sert aussi de cible aux liens (`href="#upload"` saute à cette section) et au JavaScript (`getElementById`, §5).

### 2.7 Les entités — et le lien direct avec ton code

Certains caractères ont un sens **structurel** en HTML. Pour en afficher un littéralement, on écrit une **entité** :

| Tu veux afficher | Tu écris | Pourquoi |
|---|---|---|
| `&` | `&amp;` | `&` commence une entité |
| `<` | `&lt;` | `<` commence une balise |
| `>` | `&gt;` | symétrie |
| `"` | `&quot;` | délimite les valeurs d'attribut |
| `'` | `&#39;` | idem |
| `—` | `&mdash;` | tiret cadratin |
| `→` | `&rarr;` | flèche |
| `…` | `&hellip;` | points de suspension |

Dans ta page :

```html
<a href="/cgi-bin/hello.py?name=jean&amp;id=42">
```

Le `&` de la query string **doit** être écrit `&amp;`. Écrit nu, le navigateur essaierait d'interpréter `&id` comme une entité.

**C'est exactement ce que fait `Router::htmlEscape`** (`RouterClass.cpp:15-35`) pour l'autoindex :

```cpp
if (s[i] == '&')       out += "&amp;";
else if (s[i] == '<')  out += "&lt;";
else if (s[i] == '>')  out += "&gt;";
else if (s[i] == '"')  out += "&quot;";
else if (s[i] == '\'') out += "&#39;";
```

Un fichier nommé `<script>alert(1)</script>.txt` déposé dans un dossier listé par l'autoindex : sans échappement, ton serveur injecterait du code exécutable dans la page. Avec, il affiche le nom littéralement. C'est **la** défense XSS de `SECURITY.md` §4.1 — et maintenant tu sais précisément *pourquoi* ces cinq caractères et pas d'autres.

---

## 3. Comment le CSS se raccroche au HTML

C'est le point le plus important pour comprendre ton serveur.

### 3.1 Une page = plusieurs requêtes

```html
<link rel="stylesheet" href="/style.css">
```

Cette ligne n'est **pas** du style. C'est une **instruction de téléchargement**. Quand le navigateur la rencontre en parsant le HTML, il ouvre une **nouvelle requête HTTP** :

```
1.  GET /             HTTP/1.1     ->  200, Content-Type: text/html   (le HTML)
2.  GET /style.css    HTTP/1.1     ->  200, Content-Type: text/css    (le style)
3.  GET /favicon.ico  HTTP/1.1     ->  404                            (voir 3.3)
```

**Une page affichée n'est jamais une requête.** C'est ce qui explique pourquoi ton serveur doit gérer le keep-alive, et pourquoi une seule ouverture de page dans le navigateur produit plusieurs lignes dans tes logs.

| Attribut | Rôle |
|---|---|
| `rel="stylesheet"` | la **relation** : « le fichier au bout de ce lien est une feuille de style ». Sans ça, le navigateur télécharge mais n'applique rien. |
| `href="/style.css"` | l'URL. Elle commence par `/` donc elle est **absolue depuis la racine du site** : le navigateur demandera `/style.css` quelle que soit la page courante. `href="style.css"` (sans `/`) serait relatif à la page — depuis `/uploads/`, ça demanderait `/uploads/style.css` → 404. |

### 3.2 `Content-Type` fait foi

Même si le fichier s'appelle `.css`, **c'est l'en-tête qui décide**. Si `MimeTypes::forPath` renvoyait `text/plain`, le navigateur téléchargerait le fichier, verrait « ce n'est pas du CSS » et **ignorerait tout le style**. La page s'afficherait en Times New Roman noir sur blanc.

C'est le test le plus simple de ton MIME : **si la page est stylée, `text/css` est correct.**

### 3.3 Le `/favicon.ico`

Tu verras cette requête dans tes logs sans l'avoir demandée. Tous les navigateurs réclament automatiquement `/favicon.ico` (l'icône de l'onglet). Ton serveur répond 404 — c'est **normal et sans conséquence**. Ne cherche pas d'où elle vient : c'est le navigateur, tout seul.

---

## 4. CSS — la présentation

### 4.1 La syntaxe

```css
body {
	color: #1a1a1a;
	line-height: 1.6;
}
└─┬─┘  └──┬──┘  └┬┘
sélecteur propriété valeur
```

- Le **sélecteur** dit *à quels éléments* la règle s'applique.
- Entre accolades : des paires `propriété: valeur;`, chacune terminée par `;`.

Lecture : « pour tout élément `body`, mets la couleur du texte à `#1a1a1a` et l'interligne à 1.6 ».

### 4.2 Les trois sélecteurs de ton fichier

```css
body      { ... }    /* par NOM DE BALISE : tous les <body> */
.probe    { ... }    /* par CLASSE : tous les éléments ayant class="probe" */
#delete-form { ... } /* par ID : l'unique élément ayant id="delete-form" */
```

| Écriture | Cible | Dans ton HTML |
|---|---|---|
| `body`, `h2`, `ul`, `li` | toutes les balises de ce nom | — |
| `.probe`, `.badge`, `.hint` | l'attribut `class="…"` | `<form class="probe">` |
| `#un-id` | l'attribut `id="…"` | non utilisé en CSS ici |

**Classe vs id** : une classe peut être portée par autant d'éléments qu'on veut (tes 5 formulaires sont tous `class="probe"`). Un `id` est **unique dans la page**.

Un élément peut cumuler plusieurs classes : `class="probe wide"` reçoit les règles de `.probe` **et** de `.wide`.

Les sélecteurs se combinent :

```css
.probe p     { margin: 0 0 0.75rem; }    /* les <p> A L'INTERIEUR d'un .probe */
.probe button:hover { ... }              /* un bouton de .probe SURVOLE */
```

L'espace signifie « à l'intérieur de ». `:hover` est une **pseudo-classe** : un état (survolé, focus, actif).

### 4.3 La cascade

CSS = *Cascading Style Sheets*. Quand deux règles visent le même élément, celle qui gagne est déterminée par la **spécificité** :

```
id (#foo)        >   classe (.bar)   >   balise (p)
```

À spécificité égale, **la dernière écrite gagne**. C'est pour ça que l'ordre du fichier compte.

L'**héritage** joue aussi : certaines propriétés descendent aux enfants. `color` et `font-family` posés sur `body` s'appliquent à tout le document sans être répétés. `border` ou `padding`, non.

### 4.4 Les variables — `:root` et `var()`

```css
:root {
	--ink: #1a1a1a;
	--paper: #f5f2ea;
	--accent: #b5462f;
}

a {
	color: var(--accent);
}
```

- `:root` désigne l'élément racine (`<html>`). Comme la couleur est héritée, y déclarer des variables les rend disponibles **partout**.
- `--nom` déclare une variable (les deux tirets sont obligatoires).
- `var(--nom)` la lit.

C'est l'équivalent d'un `# define` en C, mais **dynamique** : changer `--accent` dans `:root` change instantanément la couleur des liens, des badges et des boutons. Ton fichier utilise ces trois variables ; c'est ce qui garantit que le rouge des boutons est exactement le rouge des liens.

### 4.5 Le modèle de boîte

Chaque élément est une boîte à quatre couches, de l'intérieur vers l'extérieur :

```
        ┌──────────────── margin (espace EXTERIEUR, transparent)
        │  ┌───────────── border (le trait)
        │  │  ┌────────── padding (espace INTERIEUR)
        │  │  │  ┌─────── content (le texte)
        │  │  │  │
     ╔══│══│══│══│═══════════════════════╗
     ║  ┌──────────────────────────────┐ ║
     ║  │ ░░░░░░░░░░░░░░░░░░░░░░░░░░░░ │ ║
     ║  │ ░░  Pick a file           ░░ │ ║
     ║  │ ░░░░░░░░░░░░░░░░░░░░░░░░░░░░ │ ║
     ║  └──────────────────────────────┘ ║
     ╚═══════════════════════════════════╝
```

```css
.probe {
	margin: 1.25rem 0;                    /* 1.25 en haut/bas, 0 à gauche/droite */
	padding: 1rem 1.25rem;                /* 1 en haut/bas, 1.25 à gauche/droite */
	border: 1px solid rgba(0, 0, 0, 0.2); /* épaisseur, style, couleur */
}
```

Quand une propriété reçoit **deux valeurs**, c'est `vertical horizontal`. Avec **quatre**, c'est `haut droite bas gauche`, dans le sens des aiguilles d'une montre.

**Retiens la différence** : `padding` pousse le trait vers l'extérieur (l'encadré grossit), `margin` éloigne l'encadré de ses voisins.

### 4.6 Les unités

| Unité | Signification | Exemple chez toi |
|---|---|---|
| `px` | pixel, absolu | `border: 1px` |
| `rem` | ×  la taille de police **racine** (16px par défaut) | `max-width: 42rem` = 672px |
| `em` | ×  la taille de police de **l'élément courant** | `padding: 0.1em 0.35em` |
| `%` | relatif au parent | `max-width: 100%` |
| `vw` | 1 % de la largeur de la fenêtre | dans le `clamp()` du `<h1>` |

`rem` est préférable à `px` pour les tailles : si l'utilisateur augmente la taille de police par défaut de son navigateur (malvoyance), tout le layout suit.

### 4.7 Lecture de ton `style.css`

```css
body {
	margin: 0 auto;          /* 0 en haut/bas ; auto à gauche/droite = CENTRE la boîte */
	max-width: 42rem;        /* jamais plus large que ~672px : le texte reste lisible */
	padding: 2rem 1.5rem;    /* de l'air autour du contenu */
	font-family: Georgia, "Times New Roman", serif;
	color: var(--ink);
	background: var(--paper);
	line-height: 1.6;        /* interligne = 1.6 × la taille de police */
}
```

`margin: 0 auto` combiné à `max-width` est **l'idiome standard pour centrer une page**. `auto` demande au navigateur de répartir équitablement l'espace restant à gauche et à droite.

`font-family` est une **liste de repli** : Georgia si disponible, sinon Times New Roman, sinon n'importe quelle serif. Exactement comme un `PATH`.

```css
h1 {
	font-size: clamp(2rem, 1rem + 5vw, 3.5rem);
}
```

`clamp(min, souhaité, max)` : jamais moins de 2rem, jamais plus de 3.5rem, et entre les deux ça grandit avec la largeur de la fenêtre (`5vw`). Un titre qui s'adapte sans une seule *media query*.

```css
.badge {
	display: inline-block;
	background: var(--accent);
	color: var(--paper);
	padding: 0.05em 0.45em;
	border-radius: 3px;
}
```

`display` contrôle le comportement de la boîte :

| Valeur | Comportement |
|---|---|
| `block` | prend toute la largeur, provoque un retour à la ligne (`<p>`, `<div>`, `<section>`) |
| `inline` | suit le texte, ignore les largeurs et les marges verticales (`<span>`, `<a>`) |
| `inline-block` | suit le texte **mais** accepte padding et largeur — ce qu'il faut pour une pastille |

`border-radius` arrondit les angles. `color: var(--paper)` sur `background: var(--accent)` : texte clair sur fond rouge.

```css
.probe button:hover,
.probe button:focus {
	background: var(--paper);
	color: var(--accent);
}
```

Une virgule entre deux sélecteurs = « applique aux deux ». Ici on **inverse** les couleurs au survol et au focus clavier. Inclure `:focus` en plus de `:hover` est important : quelqu'un qui navigue au clavier (touche Tab) doit voir où il est.

---

## 5. JavaScript — le comportement

### 5.1 Pourquoi il y en a ici

Un formulaire HTML ne sait envoyer que **GET** et **POST**. C'est une limitation de la spécification HTML, pas de ton serveur :

```html
<form method="delete">   <!-- N'EXISTE PAS. Le navigateur retombe silencieusement sur GET. -->
```

Pour tester ta méthode `DELETE`, il faut donc fabriquer la requête **à la main**, en JavaScript. C'est la seule raison de la présence de ces 10 lignes.

### 5.2 La balise `<script>`

```html
<script>
	...
</script>
```

Placée **juste avant `</body>`**, c'est-à-dire après tout le HTML. C'est délibéré : le script commence par chercher l'élément `delete-form`. S'il s'exécutait avant que le navigateur ait construit cet élément, il ne trouverait rien. Le HTML se parse de haut en bas, dans l'ordre.

### 5.3 Le code, ligne par ligne

```js
document.getElementById("delete-form").addEventListener("submit", function (event) {
	event.preventDefault();

	var name = document.getElementById("delete-name").value;
	var out = document.getElementById("delete-result");

	out.textContent = "sending...";
	fetch("/uploads/" + encodeURIComponent(name), { method: "DELETE" })
		.then(function (response) { out.textContent = "HTTP " + response.status; })
		.catch(function (error) { out.textContent = "network error: " + error.message; });
});
```

**`document`** — l'objet qui représente l'arbre DOM de la page (§2.3). C'est la porte d'entrée vers tout le contenu.

**`getElementById("delete-form")`** — retrouve l'élément dont `id="delete-form"`, c'est-à-dire le `<form>` du DELETE. Renvoie un objet sur lequel on peut agir.

**`addEventListener("submit", function (event) { … })`** — « quand cet élément émet un événement `submit`, appelle cette fonction ».

C'est exactement un **pointeur de fonction** en C :

```c
/* En C, tu écrirais : */
void	on_submit(t_event *event)
{
	...
}

register_handler(form, "submit", &on_submit);
```

La différence : en JavaScript on peut écrire la fonction **directement à l'endroit où on la passe**, sans lui donner de nom. On appelle ça une fonction anonyme, ou *callback*.

Et le rapprochement avec ton serveur est direct : `Arole::handle_event` **est** un gestionnaire d'événement. `epoll` joue le rôle du navigateur (il détecte), `handle_event` celui du callback (il réagit). Même pattern — le Reactor — des deux côtés du réseau.

**`event.preventDefault()`** — indispensable. Par défaut, soumettre un formulaire fait **naviguer la page** (le navigateur envoie la requête lui-même et remplace l'affichage). On veut garder la page et envoyer nous-mêmes : on annule donc le comportement par défaut. Sans cette ligne, la page se rechargerait et tu ne verrais jamais le résultat.

**`document.getElementById("delete-name").value`** — sur un `<input>`, `.value` contient ce que l'utilisateur a tapé. Ici, le nom du fichier à supprimer.

**`var`** — déclare une variable. JavaScript n'a pas de types déclarés : `var name = ...` accepte n'importe quoi. (`let` et `const` sont les formes modernes ; `var` marche partout et suffit largement ici.)

**`out.textContent = "sending..."`** — écrit du texte dans un élément. `textContent` insère le texte **littéralement**, sans jamais l'interpréter comme du HTML. C'est l'équivalent JavaScript de ton `htmlEscape` : si le texte contenait `<script>`, il s'afficherait tel quel au lieu d'être exécuté.

**`fetch(url, { method: "DELETE" })`** — envoie une requête HTTP depuis le JavaScript. Le second argument est un **objet d'options** ; `{ method: "DELETE" }` remplace le GET par défaut. Sur le fil, ton serveur reçoit :

```
DELETE /uploads/notes.txt HTTP/1.1
Host: localhost:8080
...
```

**`encodeURIComponent(name)`** — percent-encode le nom de fichier. Un espace devient `%20`, un `#` devient `%23`. Sans ça, un nom contenant un `?` ou un `#` casserait l'URL. Symétrique de ton `percentDecode` dans `HttpRequest` — le client encode, le serveur décode.

**`.then(function (response) { … })`** — et c'est le point conceptuel important.

`fetch` **ne bloque pas**. Il rend la main immédiatement, avant même que la requête soit partie, en renvoyant une *promesse* : un objet qui représente « un résultat qui arrivera plus tard ». `.then(callback)` enregistre la fonction à exécuter **quand** la réponse arrivera.

Tu connais déjà ce modèle par cœur — c'est le tien :

| Ton serveur | Le navigateur |
|---|---|
| `send()` non bloquant, on rend la main | `fetch()` non bloquant, on rend la main |
| `epoll_wait` détecte que c'est prêt | le moteur JS détecte que la réponse est là |
| `handle_event` est rappelé | le callback de `.then` est appelé |
| l'état vit dans les membres (`_offset`) | l'état vit dans la *closure* (`out`) |

Le navigateur a **sa propre boucle d'événements**, exactement pour la même raison que toi : un seul fil d'exécution, donc interdiction de bloquer. Si `fetch` bloquait, l'onglet gèlerait — plus de scroll, plus de clic — comme ton serveur gèlerait ses 500 clients.

**`response.status`** — le code HTTP renvoyé : `204` si la suppression a réussi, `404` si le fichier n'existait pas, `403` sur un dossier. C'est ce que la page affiche.

**`.catch(function (error) { … })`** — si la requête échoue au niveau **réseau** (serveur injoignable, connexion coupée), `.then` n'est pas appelé et `.catch` l'est.

Attention à un piège : un **404 n'est pas une erreur réseau**. La requête a parfaitement abouti, le serveur a répondu. `.then` est donc appelé avec `status = 404`. `.catch` ne se déclenche que si la réponse n'arrive pas du tout.

---

## 6. Les formulaires — ce qu'ils produisent sur le fil

C'est la partie qui teste directement ton `Router::handlePost` et ton `Upload`.

### 6.1 Les trois attributs

```html
<form method="post" action="/uploads" enctype="multipart/form-data">
```

| Attribut | Rôle | Valeurs |
|---|---|---|
| `method` | la méthode HTTP | `get` ou `post` **uniquement** |
| `action` | l'URL cible | `/uploads` |
| `enctype` | comment encoder les champs dans le corps | 3 valeurs, ci-dessous |

Chaque `<input>` doit avoir un attribut **`name`** : c'est lui qui devient la clé dans les données envoyées. Un `input` sans `name` n'est **pas** transmis.

### 6.2 `application/x-www-form-urlencoded` — le défaut

```html
<form method="post" action="/">
```

C'est ce qui est utilisé si tu ne précises pas d'`enctype`. Sur le fil :

```
POST / HTTP/1.1
Content-Type: application/x-www-form-urlencoded
Content-Length: 24

name=jean&message=coucou
```

Les champs sont collés comme une query string, dans le **corps**. C'est le formulaire « POST / » de ta page, celui qui doit renvoyer **405** parce que `location /` n'autorise que GET.

### 6.3 `multipart/form-data` — les fichiers

```html
<form method="post" action="/uploads" enctype="multipart/form-data">
	<input type="file" name="file">
```

**Obligatoire dès qu'il y a un `<input type="file">`** : l'urlencoded ne sait pas transporter des octets binaires. Sur le fil :

```
POST /uploads HTTP/1.1
Content-Type: multipart/form-data; boundary=----WebKitFormBoundaryAbC123
Content-Length: 214

------WebKitFormBoundaryAbC123
Content-Disposition: form-data; name="file"; filename="photo.png"
Content-Type: image/png

<les octets binaires du fichier>
------WebKitFormBoundaryAbC123--
```

C'est **exactement** ce que parse `Upload::parseMultipart` :

| Sur le fil | Dans ton code |
|---|---|
| `boundary=----WebKit…` dans le `Content-Type` | `extractBoundary` (`UploadClass.cpp:23`) |
| `--<boundary>` sépare les parties | `delim = "--" + boundary` (ligne 85) |
| `filename="photo.png"` | `extractFilename` (ligne 42) |
| la ligne vide `\r\n\r\n` finit les en-têtes de partie | `body.find("\r\n\r\n", pos)` (ligne 106) |
| `--<boundary>--` termine tout | `body.compare(pos, 2, "--")` (ligne 96) |

Le **boundary** est une chaîne aléatoire choisie par le navigateur, assez longue pour ne pas apparaître par hasard dans le contenu. C'est du cadrage **par délimiteur**, avec le risque que ça implique — à l'inverse du chunked qui cadre par longueur (cf. `HTTP_REQUESTS.md`).

Deux comportements à connaître :

- Une partie **sans** `filename=` (un champ texte dans un formulaire multipart) est **ignorée** par ton parseur (`if (!rawName.empty())`, ligne 125). Voulu.
- Si l'utilisateur soumet **sans choisir de fichier**, le navigateur envoie quand même une partie, avec `filename=""`. Elle est donc ignorée aussi : tu obtiens un **201 avec une liste vide**. Cohérent.

### 6.4 `text/plain` — le corps brut

```html
<form method="post" action="/uploads/notes.txt" enctype="text/plain">
```

```
POST /uploads/notes.txt HTTP/1.1
Content-Type: text/plain
Content-Length: 29

content=hello from the browser
```

Ni multipart, ni urlencoded : `Upload::handle` retombe donc sur `handleRaw`, qui prend le nom du fichier **dans l'URL** (`notes.txt`) au lieu d'un en-tête de partie. C'est le chemin qu'on teste habituellement avec `curl --data-binary @fichier`, ici déclenché depuis le navigateur. (Attention : `curl -T` enverrait un **`PUT`**, méthode que notre parseur rejette en `501`.)

Le fichier contiendra `content=hello from the browser`, avec le nom du champ — c'est bien le corps **brut**, non interprété. C'est le but.

### 6.5 `<label>` et `for`

```html
<label for="file">Pick a file</label>
<input id="file" type="file" name="file">
```

`for` doit valoir l'`id` du champ. Deux effets : cliquer sur le texte place le curseur dans le champ, et un lecteur d'écran annonce « Pick a file » en arrivant sur l'input. Ne pas confondre `for` (lien vers l'`id`) et `name` (clé envoyée au serveur) — ils peuvent différer.

---

## 7. Ce qui se passe vraiment, du clic à l'affichage

Ouverture de `http://localhost:8080/` :

```
1.  Le navigateur ouvre une connexion TCP vers 127.0.0.1:8080
        -> ton Acceptor accepte, crée un Client

2.  GET / HTTP/1.1 + Host: localhost:8080
        -> HttpRequest parse, selectVhost matche "localhost"
        -> Router : location /, index.html, 200 text/html

3.  Le navigateur parse le HTML, construit le DOM,
    trouve <link rel="stylesheet" href="/style.css">

4.  GET /style.css   (même connexion, keep-alive)
        -> 200 text/css   -> le style s'applique, la page se redessine

5.  GET /favicon.ico
        -> 404   (normal, personne n'a mis d'icône)

6.  Le navigateur exécute le <script> : addEventListener est enregistré,
    puis plus rien ne se passe -> il attend un événement

7.  L'utilisateur clique « DELETE /uploads/… »
        -> le callback s'exécute, fetch envoie :
           DELETE /uploads/notes.txt HTTP/1.1
        -> Router::handleDelete -> std::remove -> 204
        -> .then affiche « HTTP 204 »
```

Les étapes 2, 4, 5 et 7 sont **quatre requêtes HTTP distinctes** pour une seule page. C'est le fonctionnement normal du web, et la raison pour laquelle le keep-alive existe : rouvrir une connexion TCP pour chaque fichier serait ruineux.

---

## 8. Aide-mémoire

**HTML**

```html
<balise attribut="valeur">contenu</balise>     élément complet
<balise attribut="valeur">                     void element (meta, link, input, img, br, hr)
<!-- ... -->                                   commentaire (invisible)
&amp; &lt; &gt; &quot; &#39;                    entités
id="unique"   class="reutilisable"             identification
```

**CSS**

```css
balise      { ... }       tous les <balise>
.classe     { ... }       class="classe"
#identifiant{ ... }       id="identifiant"
.a .b       { ... }       les .b à l'intérieur d'un .a
.a, .b      { ... }       les .a ET les .b
.a:hover    { ... }       état survolé

--nom: valeur;   /* déclarer */        var(--nom)   /* utiliser */
margin  = espace extérieur             padding = espace intérieur
display: block | inline | inline-block
```

**JavaScript**

```js
document.getElementById("x")           retrouver un élément
element.value                          contenu d'un <input>
element.textContent = "…"              écrire du texte (sûr, jamais interprété)
element.addEventListener("submit", f)  enregistrer un callback
event.preventDefault()                 annuler le comportement par défaut
fetch(url, { method: "DELETE" })       envoyer une requête HTTP
  .then(function (r) { r.status })     quand la réponse arrive
  .catch(function (e) { ... })         si le réseau échoue
```

---

## 9. Renvois

- `ROUTING.md` — comment `/style.css` devient `./www/style.css`, et d'où sort `text/css`.
- `HTTP_REQUESTS.md` — le parsing du `POST` que produisent ces formulaires, et le dé-chunking.
- `SECURITY.md` §4.1 — l'échappement HTML, dont §2.7 explique la raison d'être côté navigateur.
- `CGI.md` — ce que devient le corps d'un formulaire envoyé à `/cgi-bin/hello.py`.
- `www/index.html` — le tableau de bord dont ce document décrit chaque construction.
