import {HttpsError, onCall} from "firebase-functions/v2/https";
import * as logger from "firebase-functions/logger";
import {initializeApp} from "firebase-admin/app";
import {getAuth} from "firebase-admin/auth";
import {getDatabase} from "firebase-admin/database";
import {getStorage} from "firebase-admin/storage";
import * as functions from "firebase-functions";

import * as path from "path";
import * as os from "os";
import * as fs from "fs";
import {randomInt} from "crypto";
import axios from "axios";
import {onObjectFinalized} from "firebase-functions/v2/storage";
import FormData from "form-data";


initializeApp();

export const createUser = onCall(async (request) => {
  const auth = request.auth;
  const data = request.data;

  if (auth?.token.role !== "root") {
    throw new Error("Acesso negado. Apenas root pode criar usuários.");
  }

  const {email, password, nome, clienteId, role} = data;

  if (!email || !password || !nome || !clienteId || !role) {
    throw new Error("Campos obrigatórios ausentes.");
  }

  const user = await getAuth().createUser({
    email,
    password,
    displayName: nome,
  });

  await getAuth().setCustomUserClaims(user.uid, {
    clienteId,
    role,
  });

  await getDatabase()
    .ref(`clientes/${clienteId}/usuarios/${user.uid}`)
    .set({nome, email, role});

  logger.info(`Usuário criado: ${user.uid}`);

  return {uid: user.uid, message: "Usuário criado com sucesso."};
});

export const deleteUser = onCall(async (request) => {
  const auth = request.auth;
  const data = request.data;

  if (auth?.token.role !== "root") {
    throw new Error("Acesso negado. Apenas root pode excluir usuários.");
  }

  const {uid, clienteId} = data;

  if (!uid || !clienteId) {
    throw new Error("UID e clienteId são obrigatórios.");
  }

  // Remove do Auth
  await getAuth().deleteUser(uid);

  // Remove do RTDB
  await getDatabase().ref(`clientes/${clienteId}/usuarios/${uid}`).remove();

  return {message: "Usuário excluído com sucesso."};
});

export const editUser = onCall(async (request) => {
  const auth = request.auth;
  const data = request.data;

  if (auth?.token.role !== "root") {
    throw new Error("Acesso negado.");
  }

  const {uid, clienteId, nome, role} = data;

  if (!uid || !clienteId || !nome || !role) {
    throw new Error("Campos obrigatórios ausentes.");
  }

  // Atualiza no RTDB
  await getDatabase().ref(`clientes/${clienteId}/usuarios/${uid}`).update({
    nome,
    role,
  });

  // Atualiza displayName no Auth (opcional)
  await getAuth().updateUser(uid, {displayName: nome});

  return {message: "Usuário atualizado com sucesso."};
});

/**
 * Verifica se o erro possui uma propriedade 'code',
 * indicando um erro do Firebase Auth.
 * @param {unknown} err - Objeto de erro a ser verificado.
 * @return {boolean} Retorna true se o erro possui uma propriedade 'code'.
 */
function isFirebaseAuthError(err: unknown): err is { code: string } {
  return typeof err === "object" && err !== null && "code" in err;
}

/**
 * Verifica se o erro possui uma propriedade 'code',
 * indicando um erro do Firebase Auth.
 * @param {string} tamanho bjeto de erro a ser verificado.
 * @return {string} senha Retorna true se o erro possui uma propriedade 'code'.
 */
function gerarSenhaAleatoria(tamanho = 16): string {
  const caracteres = "ABCDEFGHIJKLMNOPQRSTUVWXYZ" +
  "abcdefghijklmnopqrstuvwxyz0123456789!@#$%^&*()_+-=";
  let senha = "";
  for (let i = 0; i < tamanho; i++) {
    senha += caracteres.charAt(randomInt(caracteres.length));
  }
  return senha;
}

export const createDeviceV2 = functions.https.onRequest(async (req, res) => {
  if (req.method !== "POST") {
    res.status(405).send("Method Not Allowed");
    return;
  }

  const {sn, mac} = req.body;


  if (!sn || !mac) {
    res.status(400).send(
      "Campos obrigatórios ausentes: sn ou mac");
    return;
  }

  try {
    const normalizedSn = sn.trim().toUpperCase();
    const nome = "AGSN" + normalizedSn;
    const descricao = "Provisionamento Automatico";
    const senhaGerada = gerarSenhaAleatoria();


    // Busca direta no caminho mapaSn/{sn}
    const snRef = await getDatabase().ref(
      `mapaSn/${normalizedSn}`).get();

    if (!snRef.exists()) {
      res.status(404).send("SN não encontrado no sistema.");
      return;
    }

    const {clienteId} = snRef.val();

    if (!clienteId) {
      res.status(400).send("ClienteId não encontrado para este SN.");
      return;
    }

    const dispositivoId = normalizedSn;
    const email = `${dispositivoId}@agroscan.com`;

    try {
      await getAuth().createUser({
        uid: dispositivoId,
        email: email,
        password: senhaGerada});
    } catch (error) {
      if (isFirebaseAuthError(error)) {
        if (error.code !== "auth/uid-already-exists") {
          console.error("Erro ao criar usuário:", error);
          res.status(500).send("Erro ao criar usuário.");
          return;
        }
        // Se for 'auth/uid-already-exists', simplesmente ignora e segue.
      } else {
        console.error("Erro desconhecido:", error);
        res.status(500).send("Erro desconhecido ao criar usuário.");
        return;
      }
    }
    // Definir claims personalizados no usuário
    await getAuth().setCustomUserClaims(dispositivoId, {
      clienteId,
      role: "dispositivo",
    });

    // Registrar o dispositivo no nó definitivo
    await getDatabase().ref(
      `clientes/${clienteId}/dispositivos/${dispositivoId}`).set({
      mac,
      nome,
      descricao: descricao || "",
      criadoEm: Date.now(),
    });

    // Remove do mapaSn após cadastro
    await getDatabase().ref(`mapaSn/${dispositivoId}`).remove();

    // Gera o custom token
    // const customToken = await getAuth().createCustomToken(dispositivoId);

    res.status(200).send({
      uid: dispositivoId,
      clienteId: clienteId,
      email: email,
      senha: senhaGerada,
      message: "Dispositivo criado com sucesso.",
    });
  } catch (error) {
    console.error("Erro ao criar dispositivo:", JSON.stringify(error));
    res.status(500).send(JSON.stringify(error));
  }
});

/**
 * Verifica se o SN é uma chave válida para o RTDB e para o prefixo do Storage.
 * @param {unknown} sn - Valor recebido do cliente.
 * @return {boolean} Retorna true se o SN for uma string segura.
 */
function isSnValido(sn: unknown): sn is string {
  return typeof sn === "string" && /^[A-Za-z0-9_-]+$/.test(sn.trim());
}

export const deleteDevice = onCall(async (request) => {
  const auth = request.auth;
  const role = auth?.token.role;

  if (role !== "root" && role !== "admin_cliente") {
    throw new HttpsError("permission-denied", "Acesso negado.");
  }

  const {sn} = request.data;

  if (!isSnValido(sn)) {
    throw new HttpsError("invalid-argument", "Campo SN inválido.");
  }

  const normalizedSn = sn.trim().toUpperCase();

  // Busca o clienteId no RTDB
  const snapshot = await getDatabase().ref(`mapaSn/${normalizedSn}`).get();

  let clienteId = null;

  if (snapshot.exists()) {
    clienteId = snapshot.val()?.clienteId;
  } else {
    // Caso já não esteja no mapaSn, tenta buscar no nó definitivo
    const allClientesSnap = await getDatabase().ref(
      "clientes").once("value");
    const allClientes = allClientesSnap.val();
    for (const cid in allClientes) {
      if (Object.prototype.hasOwnProperty.call(allClientes, cid)) {
        const dispositivos = allClientes[cid]?.dispositivos || {};
        if (dispositivos[normalizedSn]) {
          clienteId = cid;
          break;
        }
      }
    }
  }

  if (!clienteId) {
    throw new HttpsError("not-found", "Dispositivo não encontrado.");
  }

  // admin_cliente só pode excluir dispositivos do próprio cliente
  if (role !== "root" && auth?.token.clienteId !== clienteId) {
    throw new HttpsError("permission-denied", "Acesso negado.");
  }

  // Deleta do Authentication
  try {
    await getAuth().deleteUser(normalizedSn);
  } catch (err) {
    if (!isFirebaseAuthError(err) || err.code !== "auth/user-not-found") {
      throw err;
    }
    // Usuário não encontrado, ignora
  }

  // Deleta do RTDB
  await getDatabase().ref(
    `clientes/${clienteId}/dispositivos/${normalizedSn}`
  ).remove();

  // Deleta do mapaSn, se existir
  await getDatabase().ref(`mapaSn/${normalizedSn}`).remove();

  // Deletar fotos, dados, ou Storage
  await getStorage().bucket().deleteFiles({
    prefix: `clientes/${clienteId}/dispositivos/${normalizedSn}/`,
  });

  logger.info(`Dispositivo excluído: ${normalizedSn} (cliente ${clienteId})`);

  return {
    sn: normalizedSn,
    clienteId,
    message: "Dispositivo excluído com sucesso.",
  };
});

export const onImageUpload = onObjectFinalized(
  // {
  //   region: "us-central1",
  //   bucket: "agroscan-c8a09.appspot.com",
  // },
  async (event) => {
    const filePath = event.data?.name || "";

    if (!filePath.endsWith(".jpg") || !filePath.includes("/fotos/")) {
      console.log("Ignorado: não é uma imagem jpg na pasta /fotos/");
      return;
    }

    console.log(`Nova imagem detectada: ${filePath}`);

    const pathParts = filePath.split("/");
    const clienteId = pathParts[1];
    const dispositivoId = pathParts[3];
    const fileName = pathParts[pathParts.length - 1];

    const bucket = getStorage().bucket();
    const tempFilePath = path.join(os.tmpdir(), fileName);

    // Baixar imagem temporariamente
    await bucket.file(filePath).download({destination: tempFilePath});
    console.log("Imagem baixada localmente:", tempFilePath);

    // Enviar ao Azure
    const formData = new FormData();
    formData.append("image", fs.createReadStream(tempFilePath), {
      contentType: "image/jpeg",
      filename: fileName,
    });

    let resultado;
    try {
      const response = await axios.post("https://agroscan.azurewebsites.net/api/predict", formData, {
        headers: formData.getHeaders(),
      });
      resultado = response.data;
      console.log("Classificação recebida:", resultado);
    } catch (error) {
      console.error("Erro ao chamar Azure Function:", error);
      return;
    }

    const resultPath = (
      `/clientes/${clienteId}/dispositivos/${dispositivoId}` +
    `/dados/${fileName.replace(".jpg", "")}/resultado/`
    );

    await getDatabase().ref(resultPath).set(resultado);
    console.log(`Resultado salvo em ${resultPath}`);

    fs.unlinkSync(tempFilePath);
  });
