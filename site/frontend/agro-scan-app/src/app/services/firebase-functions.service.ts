import { Injectable } from '@angular/core';
import { Functions, httpsCallable } from '@angular/fire/functions';

@Injectable({
  providedIn: 'root'
})
export class FirebaseFunctionsService {

  constructor(private functions: Functions) {}

  /**
   * Chama a função deleteDevice para remover um dispositivo.
   * A chamada é autenticada com o token do usuário logado.
   * @param sn Número de série (SN) do dispositivo.
   * @returns Promise com a resposta da função.
   */
  async deleteDevice(sn: string) {
    const deleteDevice = httpsCallable(this.functions, 'deleteDevice');
    const result = await deleteDevice({ sn: sn.trim().toUpperCase() });
    return result.data;
  }
}
